#pragma once

#include "ttio.h"

#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <utility>

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

#define OK(x) \
do { \
    int ret = (x); \
    if (ret != 0) { \
        fprintf(stderr, "Error: %s: %d\n", #x, ret); \
        exit(1); \
    } \
} while (0)

namespace tt {

class Tensix
{
    tt_device_t* device;

    tt_tlb_t* reg_tlb;
    tt_tlb_t* mem_tlb;

    void* mmio_reg;
    void* mmio_mem;

    tt_noc_params_t* reg_params;
    tt_noc_params_t* mem_params;

public:
    Tensix(tt_device_t* device)
    {
        OK(tt_tlb_alloc(device, 1 << 21, TT_MMIO_CACHE_MODE_UC, &reg_tlb));
        OK(tt_tlb_get_mmio(reg_tlb, &mmio_reg));

        OK(tt_tlb_alloc(device, 1 << 21, TT_MMIO_CACHE_MODE_WC, &mem_tlb));
        OK(tt_tlb_get_mmio(mem_tlb, &mmio_mem));

        OK(tt_noc_params_alloc(&reg_params));
        OK(tt_noc_params_alloc(&mem_params));
    }

    void aim(uint16_t x, uint16_t y)
    {
        // OK(tt_tlb_set(mem_tlb, x, y, TENSIX_MEM_BASE));
        // OK(tt_tlb_set(reg_tlb, x, y, TENSIX_REG_BASE));
    }

    void aim(uint16_t x_end, uint16_t y_end, uint16_t x_start, uint16_t y_start, uint8_t noc)
    {
        OK(tt_noc_params_set_xy_end(reg_params, x_end, y_end));
        OK(tt_noc_params_set_xy_start(reg_params, x_start, y_start));
        OK(tt_noc_params_set_noc(reg_params, noc));
        OK(tt_noc_params_set_mcast(reg_params, true));
        OK(tt_noc_params_set_ordering(reg_params, TT_NOC_ORDERING_STRICT));

        OK(tt_noc_params_set_xy_end(mem_params, x_end, y_end));
        OK(tt_noc_params_set_xy_start(mem_params, x_start, y_start));
        OK(tt_noc_params_set_noc(mem_params, noc));
        OK(tt_noc_params_set_mcast(mem_params, true));
        OK(tt_noc_params_set_ordering(mem_params, TT_NOC_ORDERING_STRICT));

        OK(tt_tlb_set_params(reg_tlb, reg_params));
        OK(tt_tlb_set_params(mem_tlb, mem_params));
    }

    void simple()
    {
        static constexpr uint32_t instructions[] = {
            0x001002b7, // lui	t0,0x100
            0x00028293, // mv	t0,t0
            0xdeadc337, // lui	t1,0xdeadc
            0xeef30313, // addi	t1,t1,-273 # deadbeef
            0x0062a023, // sw	t1,0(t0) # 100000
        };
        memcpy(mmio_mem, instructions, sizeof(instructions));
    }

    void load_mem(uint64_t addr, const void* src, size_t size)
    {
    }
    
    void start()
    {
    }

    void stop()
    {
    }

    ~Tensix()
    {
        tt_tlb_free(reg_tlb);
        tt_tlb_free(mem_tlb);
        tt_noc_params_free(reg_params);
        tt_noc_params_free(mem_params);
    }
};

class DmaBuffer
{
    tt_device_t* device;
    void* mem;
    size_t len;
    uint64_t iova;
    uint64_t noc_addr;

public:
    DmaBuffer(tt_device_t* device, size_t len)
        : device(device)
        , mem(MAP_FAILED)
        , len(len)
    {
        if (len % 4096 != 0) {
            fprintf(stderr, "Error: buffer size must be a multiple of 4KB\n");
            exit(1);
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
            fprintf(stderr, "Error: failed to allocate DMA buffer\n");
            exit(1);
        }

        OK(tt_dma_map(device, mem, len, &iova, &noc_addr));
    }

    template <typename T> T* get_mem() { return (T*)(mem); }
    uint64_t get_iova() const { return iova; }
    uint64_t get_noc_addr() const { return noc_addr; }
    size_t get_len() const { return len; }

    ~DmaBuffer()
    {
        munmap(mem, len);
        OK(tt_dma_unmap(device, mem, len));
    }
};

class Device
{
    tt_device_t* device;
    uint32_t driver_version;
    uint16_t vendor_id;
    uint16_t device_id;
    uint16_t pci_domain;
    uint16_t pci_bus;
    uint16_t pci_device;
    uint16_t pci_function;

public:
    Device(const char* chardev_path)
    {
        OK(tt_device_open(chardev_path, &device));
        OK(tt_driver_get_attr(device, TT_DRIVER_ATTR_VERSION, &driver_version));

        if (driver_version < 2) {
            fprintf(stderr, "Error: driver version %d is too old\n", driver_version);
            exit(1);
        }

        OK(tt_device_get_attr(device, TT_DEVICE_ATTR_PCI_VENDOR_ID, &vendor_id));
        OK(tt_device_get_attr(device, TT_DEVICE_ATTR_PCI_DEVICE_ID, &device_id));
        OK(tt_device_get_attr(device, TT_DEVICE_ATTR_PCI_DOMAIN, &pci_domain));
        OK(tt_device_get_attr(device, TT_DEVICE_ATTR_PCI_BUS, &pci_bus));
        OK(tt_device_get_attr(device, TT_DEVICE_ATTR_PCI_DEVICE, &pci_device));
        OK(tt_device_get_attr(device, TT_DEVICE_ATTR_PCI_FUNCTION, &pci_function));

        printf("Device: %04x:%04x; driver: %d\n", vendor_id, device_id, driver_version);
        printf("PCI: %04x:%02x:%02x.%x\n", pci_domain, pci_bus, pci_device, pci_function);
    }

    tt_device_t* handle() const { return device; }
    bool is_wormhole() const { return device_id == WORMHOLE_PCI_DEVICE_ID; }
    bool is_blackhole() const { return device_id == BLACKHOLE_PCI_DEVICE_ID; }

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
        OK(tt_noc_read32(device, x, y, addr, &value));
        return value;
    }
    
    void noc_write32(uint16_t x, uint16_t y, uint64_t addr, uint32_t value)
    {
        OK(tt_noc_write32(device, x, y, addr, value));
    }

    void noc_write(uint16_t x, uint16_t y, uint64_t addr, const void* src, size_t size)
    {
        OK(tt_noc_write(device, x, y, addr, src, size));
    }

    void noc_read(uint16_t x, uint16_t y, uint64_t addr, void* dst, size_t size)
    {
        OK(tt_noc_read(device, x, y, addr, dst, size));
    }

    uint32_t read_bh_telemetry(uint32_t tag)
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

        fprintf(stderr, "Error: telemetry tag %d not found\n", tag);
        exit(1);
        return ~0U;
    }

    ~Device()
    {
        tt_device_close(device);
    }
};


} // namespace tt
