#pragma once

#include <tenstorrent/ttkmd.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <system_error>
#include <utility>
#include <vector>

#include <stdio.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdbool.h>
#include <linux/mman.h>

namespace tt {

class DmaBuffer
{
    tt_device_t* device;
    tt_dma_t* dma;
    void* mem;
    size_t len;
    uint64_t iova;
    uint64_t noc_addr;

public:
    DmaBuffer(tt_device_t* device, size_t len)
        : device(device)
        , mem(MAP_FAILED)
        , len(len)
        , iova(~0ULL)
        , noc_addr(~0ULL)
    {
        if (len % getpagesize() != 0) {
            throw std::invalid_argument("Buffer size must be a multiple of page size");
        }

        if (len % (1ULL << 30) == 0) {
            mem = mmap(0, len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB | MAP_HUGE_1GB, -1, 0);
        }

        if (mem == MAP_FAILED && (len % (1ULL << 21) == 0)) {
            mem = mmap(0, len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB | MAP_HUGE_2MB, -1, 0);
        }

        if (mem == MAP_FAILED) {
            mem = mmap(0, len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        }

        if (mem == MAP_FAILED) {
            throw std::system_error(errno, std::generic_category(), "Failed to allocate DMA buffer");
        }

        int r = tt_dma_map(device, mem, len, TT_DMA_FLAG_NOC, &dma);
        if (r) {
            munmap(mem, len);
            throw std::system_error(r, std::generic_category(), "Failed to map DMA buffer");
        }

        r = tt_dma_get_dma_addr(dma, &iova);
        if (r) {
            tt_dma_unmap(device, dma);
            munmap(mem, len);
            throw std::system_error(r, std::generic_category(), "Failed to get DMA address");
        }

        r = tt_dma_get_noc_addr(dma, &noc_addr);
        if (r) {
            tt_dma_unmap(device, dma);
            munmap(mem, len);
            throw std::system_error(r, std::generic_category(), "Failed to get NOC address");
        }
    }

    void* get_mem() { return mem; }
    uint64_t get_iova() const { return iova; }
    uint64_t get_noc_addr() const { return noc_addr; }
    size_t get_len() const { return len; }

    ~DmaBuffer()
    {
        munmap(mem, len);
        tt_dma_unmap(device, dma);
    }
};

class Device
{
    tt_device_t* device{nullptr};
    std::string path;
    uint64_t driver_version{0};
    uint64_t device_arch{TT_DEVICE_ARCH_UNKNOWN};
    uint64_t vendor_id{0};
    uint64_t device_id{0};
    uint64_t pci_domain{0};
    uint64_t pci_bus{0};
    uint64_t pci_device{0};
    uint64_t pci_function{0};

public:
    Device(const char* chardev_path)
    {
        path = chardev_path;

        int r = tt_device_open(chardev_path, &device);
        if (r) {
            throw std::system_error(r, std::generic_category(), "Failed to open device");
        }

        tt_driver_get_attr(device, TT_DRIVER_API_VERSION, &driver_version);
        tt_device_get_attr(device, TT_DEVICE_ATTR_CHIP_ARCH, &device_arch);

        if (driver_version < 2) {
            throw std::runtime_error("Driver version is too old");
        }


        tt_device_get_attr(device, TT_DEVICE_ATTR_PCI_VENDOR_ID, &vendor_id);
        tt_device_get_attr(device, TT_DEVICE_ATTR_PCI_DEVICE_ID, &device_id);
        tt_device_get_attr(device, TT_DEVICE_ATTR_PCI_DOMAIN, &pci_domain);
        tt_device_get_attr(device, TT_DEVICE_ATTR_PCI_BUS, &pci_bus);
        tt_device_get_attr(device, TT_DEVICE_ATTR_PCI_DEVICE, &pci_device);
        tt_device_get_attr(device, TT_DEVICE_ATTR_PCI_FUNCTION, &pci_function);

    }

    tt_device_t* handle() const { return device; }
    bool is_wormhole() const { return device_arch == TT_DEVICE_ARCH_WORMHOLE; }
    bool is_blackhole() const { return device_arch == TT_DEVICE_ARCH_BLACKHOLE; }

    std::string get_path() const { return path; }
    uint16_t get_vendor_id() const { return static_cast<uint16_t>(vendor_id); }
    uint16_t get_device_id() const { return static_cast<uint16_t>(device_id); }
    uint64_t get_pci_domain() const { return pci_domain; }
    uint64_t get_pci_bus() const { return pci_bus; }
    uint64_t get_pci_device() const { return pci_device; }
    uint64_t get_pci_function() const { return pci_function; }

    std::pair<uint16_t, uint16_t> get_pcie_coordinates() const
    {
        if (is_wormhole()) {
            return {0, 3};
        } else if (is_blackhole()) {
            return {19, 24};
        }
        return {-1, -1};
    }

    std::pair<uint16_t, uint16_t> get_noc_grid_size() const
    {
        if (is_wormhole()) {
            return {10, 12};
        } else if (is_blackhole()) {
            return {17, 12};
        }
        return {-1, -1};
    }

    uint32_t noc_read32(uint16_t x, uint16_t y, uint64_t addr)
    {
        uint32_t value;
        int r = tt_noc_read32(device, x, y, addr, &value);
        if (r) {
            throw std::system_error(r, std::generic_category(), "Failed to read NOC address");
        }
        return value;
    }

    void noc_write32(uint16_t x, uint16_t y, uint64_t addr, uint32_t value)
    {
        int r = tt_noc_write32(device, x, y, addr, value);
        if (r) {
            throw std::system_error(r, std::generic_category(), "Failed to write NOC address");
        }
    }

    void noc_write(uint16_t x, uint16_t y, uint64_t addr, const void* src, size_t size)
    {
        int r = tt_noc_write(device, x, y, addr, src, size);
        if (r) {
            throw std::system_error(r, std::generic_category(), "Failed to write NOC address");
        }
    }

    void noc_read(uint16_t x, uint16_t y, uint64_t addr, void* dst, size_t size)
    {
        int r = tt_noc_read(device, x, y, addr, dst, size);
        if (r) {
            throw std::system_error(r, std::generic_category(), "Failed to read NOC address");
        }
    }

    uint32_t read_telemetry(uint32_t tag)
    {
        if (is_wormhole()) {
            return ~0U;
        }

        static constexpr auto SCRATCH_RAM = [](int N){ return 0x80030400 + (N * 4); };
        static constexpr uint64_t ARC_TELEMETRY_PTR = SCRATCH_RAM(13);
        static constexpr uint64_t ARC_TELEMETRY_DATA = SCRATCH_RAM(12);
        static constexpr uint32_t ARC_X = 8;
        static constexpr uint32_t ARC_Y = 0;

        uint64_t base_addr = noc_read32(ARC_X, ARC_Y, ARC_TELEMETRY_PTR);
        uint64_t data_addr = noc_read32(ARC_X, ARC_Y, ARC_TELEMETRY_DATA);
        uint32_t num_entries = noc_read32(ARC_X, ARC_Y, base_addr + 4);

        for (uint32_t i = 0; i < num_entries; ++i) {
            uint32_t tag_entry = noc_read32(ARC_X, ARC_Y, base_addr + 8 + (i * 4));
            uint16_t tag_id = tag_entry & 0xFFFF;
            uint16_t offset = (tag_entry >> 16) & 0xFFFF;
            uint64_t addr = data_addr + (offset * 4);

            if (tag_id == tag) {
                return noc_read32(ARC_X, ARC_Y, addr);
            }
        }

        return ~0U; // Not found
    }

    ~Device()
    {
        tt_device_close(device);
    }
};

static inline std::vector<std::string> enumerate_devices()
{
    std::vector<std::string> devices;
    for (const auto& entry : std::filesystem::directory_iterator("/dev/tenstorrent/")) {
        if (std::filesystem::is_character_file(entry.path()) || std::filesystem::is_block_file(entry.path())) {
            devices.push_back(entry.path().string());
        }
    }

    std::sort(devices.begin(), devices.end());
    return devices;
}

static inline void print_device_info(const tt::Device& device)
{
    std::cout << "--- Device: " << device.get_path()
            << (device.is_blackhole() ? " (Blackhole)" : "")
            << (device.is_wormhole() ? " (Wormhole)" : "");

    std::cout << "  PCI: " << std::hex << std::setfill('0')
            << std::setw(4) << device.get_pci_domain()   << ":"
            << std::setw(2) << device.get_pci_bus()      << ":"
            << std::setw(2) << device.get_pci_device()   << "."
            << std::setw(1) << device.get_pci_function() << std::dec
            << " ---" << std::endl;
}


} // namespace tt
