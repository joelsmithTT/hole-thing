/**
 * SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * @file holething.hpp
 * @brief ttkmd.h C++ wrapper, associated routines and utilities
 *
 * NB: this code is often broken, caveat emptor.
 */

#pragma once

#include "ttkmd.h"

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

// Supports Wormhole and Blackhole architectures.
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
            throw std::system_error(-r, std::generic_category(), "Failed to open device");
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

    uint64_t get_vendor_id() const { return vendor_id; }
    uint64_t get_device_id() const { return device_id; }
    uint64_t get_pci_domain() const { return pci_domain; }
    uint64_t get_pci_bus() const { return pci_bus; }
    uint64_t get_pci_device() const { return pci_device; }
    uint64_t get_pci_function() const { return pci_function; }

    uint32_t noc_read32(uint16_t x, uint16_t y, uint64_t addr)
    {
        uint32_t value;
        int r = tt_noc_read32(device, x, y, addr, &value);
        if (r) {
            throw std::system_error(-r, std::generic_category(), "Failed to read NOC address");
        }
        return value;
    }

    void noc_write32(uint16_t x, uint16_t y, uint64_t addr, uint32_t value)
    {
        int r = tt_noc_write32(device, x, y, addr, value);
        if (r) {
            throw std::system_error(-r, std::generic_category(), "Failed to write NOC address");
        }
    }

    void noc_write(uint16_t x, uint16_t y, uint64_t addr, const void* src, size_t size)
    {
        int r = tt_noc_write(device, x, y, addr, src, size);
        if (r) {
            throw std::system_error(-r, std::generic_category(), "Failed to write NOC address");
        }
    }

    void noc_read(uint16_t x, uint16_t y, uint64_t addr, void* dst, size_t size)
    {
        int r = tt_noc_read(device, x, y, addr, dst, size);
        if (r) {
            throw std::system_error(-r, std::generic_category(), "Failed to read NOC address");
        }
    }

    uint32_t read_telemetry(uint32_t tag)
    {
        auto [ARC_X, ARC_Y] = get_arc_coordinates();
        auto [ARC_TELEMETRY_PTR, ARC_TELEMETRY_DATA] = get_telemetry_pointers();

        uint64_t base_addr = noc_read32(ARC_X, ARC_Y, ARC_TELEMETRY_PTR);
        uint64_t data_addr = noc_read32(ARC_X, ARC_Y, ARC_TELEMETRY_DATA);

        if (is_wormhole()) {
            base_addr |= 0x8'0000'0000ULL;
            data_addr |= 0x8'0000'0000ULL;
        }

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

    uint32_t read_scratch(uint32_t index)
    {
        if (is_blackhole()) {
            // TODO
            return ~0U;
        }
        auto [ARC_X, ARC_Y] = get_arc_coordinates();
        uint64_t SCRATCH_BASE = 0x880030060;

        return noc_read32(ARC_X, ARC_Y, SCRATCH_BASE + (index * sizeof(uint32_t)));
    }

    std::pair<uint16_t, uint16_t> get_pcie_coordinates() const
    {
        if (is_wormhole()) {
            return {0, 3};
        } else if (is_blackhole()) {
            return {19, 24};
        }
        throw std::runtime_error("Unknown device architecture");
        return {-1, -1};
    }

    std::pair<uint16_t, uint16_t> get_arc_coordinates() const
    {
        if (is_wormhole()) {
            return {0, 10};
        } else if (is_blackhole()) {
            return {8, 0};
        }
        throw std::runtime_error("Unknown device architecture");
        return {-1, -1};
    }

    std::pair<uint16_t, uint16_t> get_noc_grid_size() const
    {
        if (is_wormhole()) {
            return {10, 12};
        } else if (is_blackhole()) {
            return {17, 12};
        }
        throw std::runtime_error("Unknown device architecture");
        return {-1, -1};
    }

    std::pair<uint64_t, uint64_t> get_telemetry_pointers() const
    {
        if (is_wormhole()) {
            return {0x8'8003'01D0ULL, 0x8'8003'01D4ULL};
        } else if (is_blackhole()) {
            return {0x0'8003'0434ULL, 0x0'8003'0430ULL};
        }
        throw std::runtime_error("Unknown device architecture");
        return {~0ULL, ~0ULL};
    }

    ~Device()
    {
        tt_device_close(device);
    }

private:
    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;
    Device(Device&&) = delete;
    Device& operator=(Device&&) = delete;
};

class DeviceUtils
{
public:
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

    static inline int noc_sanity_check(Device& device)
    {
        if (device.is_blackhole()) {
            static constexpr uint64_t NOC_NODE_ID_LOGICAL = 0xffb20148ULL;

            auto is_tensix_bh = [](uint32_t x, uint32_t y) -> bool {
                return (y >= 2 && y <= 11) &&   // Valid y range
                    ((x >= 1 && x <= 7) ||      // Left block
                    (x >= 10 && x <= 16));      // Right block
            };

            auto [size_x, size_y] = device.get_noc_grid_size();
            for (uint32_t x = 0; x < size_x; ++x) {
                for (uint32_t y = 0; y < size_y; ++y) {

                    if (!is_tensix_bh(x, y))
                        continue;

                    uint32_t node_id = device.noc_read32(x, y, NOC_NODE_ID_LOGICAL);
                    uint32_t node_id_x = (node_id >> 0x0) & 0x3f;
                    uint32_t node_id_y = (node_id >> 0x6) & 0x3f;

                    if (node_id_x != x || node_id_y != y) {
                        return -1;
                    }
                }
            }

            return 0;
        }

        if (device.is_wormhole()) {
            {
                constexpr uint32_t ARC_X = 0;
                constexpr uint32_t ARC_Y = 10;
                constexpr uint64_t ARC_NOC_NODE_ID = 0xFFFB2002CULL;

                auto node_id = device.noc_read32(ARC_X, ARC_Y, ARC_NOC_NODE_ID);
                auto x = (node_id >> 0x0) & 0x3f;
                auto y = (node_id >> 0x6) & 0x3f;
                if (x != ARC_X || y != ARC_Y) {
                    return -1;
                }
            }

            {
                constexpr uint32_t DDR_X = 0;
                constexpr uint32_t DDR_Y = 11;
                constexpr uint64_t DDR_NOC_NODE_ID = 0x10009002CULL;

                auto node_id = device.noc_read32(DDR_X, DDR_Y, DDR_NOC_NODE_ID);
                auto x = (node_id >> 0x0) & 0x3f;
                auto y = (node_id >> 0x6) & 0x3f;
                if (x != DDR_X || y != DDR_Y) {
                    return -1;
                }
            }

            constexpr uint64_t TENSIX_NOC_NODE_ID = 0xffb2002cULL;
            auto is_tensix_wh = [](uint32_t x, uint32_t y) -> bool {
                return (((y != 6) && (y >= 1) && (y <= 11)) && // valid Y
                        ((x != 5) && (x >= 1) && (x <= 9)));   // valid X
            };
            for (uint32_t x = 0; x < 12; ++x) {
                for (uint32_t y = 0; y < 12; ++y) {
                    if (!is_tensix_wh(x, y)) {
                        continue;
                    }
                    auto node_id = device.noc_read32(x, y, TENSIX_NOC_NODE_ID);
                    auto node_id_x = (node_id >> 0x0) & 0x3f;
                    auto node_id_y = (node_id >> 0x6) & 0x3f;

                    if (node_id_x != x || node_id_y != y) {
                        return -1;
                    }
                }
            }
            return 0;
        }

        return -1;
    }
};

// TlbWindow is a mapping to the device NOC.
class TlbWindow
{
    Device& device;
    size_t size;
    tt_tlb_t* tlb{nullptr};

public:
    TlbWindow(Device& device, size_t size, enum tt_tlb_cache_mode cache = TT_MMIO_CACHE_MODE_UC)
        : device(device)
        , size(size)
    {
        int r = tt_tlb_alloc(device.handle(), size, cache, &tlb);
        if (r) {
            throw std::system_error(-r, std::generic_category(), "Failed to open TLB window");
        }
    }

    size_t get_size() const { return size; }

    void* get_mmio() const
    {
        void* mmio;
        int r = tt_tlb_get_mmio(tlb, &mmio);
        if (r) {
            throw std::system_error(-r, std::generic_category(), "Failed to get TLB MMIO");
        }
        return mmio;
    }

    // Address must be aligned to the TLB size to map.
    void map(uint8_t x, uint8_t y, uint64_t addr)
    {
        int r = tt_tlb_map_unicast(device.handle(), tlb, x, y, addr);
        if (r) {
            throw std::system_error(-r, std::generic_category(), "Failed to map TLB window");
        }
    }

    void map(uint8_t start_x, uint8_t start_y, uint8_t end_x, uint8_t end_y, uint64_t addr,
             bool multicast = false, uint8_t ordering = 0, bool static_vc = false)
    {
        tt_noc_addr_config_t config = {};
        config.addr = addr;
        config.x_start = start_x;
        config.y_start = start_y;
        config.x_end = end_x;
        config.y_end = end_y;
        config.noc = 0; // NOC0
        config.mcast = multicast ? 1 : 0;
        config.ordering = ordering;
        config.static_vc = static_vc ? 1 : 0;

        int r = tt_tlb_map(device.handle(), tlb, &config);
        if (r) {
            throw std::system_error(-r, std::generic_category(), "Failed to map TLB window");
        }
    }

    uint32_t read32(off_t offset)
    {
        if (offset % 4 != 0) {
            throw std::invalid_argument("Misaligned");
        }

        return *(volatile uint32_t*)((uint8_t*)get_mmio() + offset);
    }

    void write32(off_t offset, uint32_t value)
    {
        if (offset % 4 != 0) {
            throw std::invalid_argument("Misaligned");
        }

        *(volatile uint32_t*)((uint8_t*)get_mmio() + offset) = value;
    }

    ~TlbWindow()
    {
        tt_tlb_free(device.handle(), tlb);
    }

private:
    TlbWindow(const TlbWindow&) = delete;
    TlbWindow& operator=(const TlbWindow&) = delete;
    TlbWindow(TlbWindow&&) = delete;
    TlbWindow& operator=(TlbWindow&&) = delete;
};

class TlbWindowUtils
{
public:
    static inline uint32_t noc_read32(TlbWindow& tlb, uint8_t x, uint8_t y, uint64_t addr)
    {
        if (addr % 4 != 0) {
            throw std::invalid_argument("Misaligned");
        }

        tlb.map(x, y, addr & ~(tlb.get_size() - 1));
        return tlb.read32(addr & (tlb.get_size() - 1));
    }

    static void noc_write32(TlbWindow& tlb, uint8_t x, uint8_t y, uint64_t addr, uint32_t value)
    {
        if (addr % 4 != 0) {
            throw std::invalid_argument("Misaligned");
        }

        tlb.map(x, y, addr & ~(tlb.get_size() - 1));
        tlb.write32(addr & (tlb.get_size() - 1), value);
    }

    static void noc_read(TlbWindow& tlb, uint8_t x, uint8_t y, uint64_t addr, void* dst, size_t len)
    {
        if (addr % 4 != 0 || len % 4 != 0) {
            throw std::invalid_argument("Misaligned");
        }

        uint8_t* dst_ptr = (uint8_t*)dst;

        while (len > 0) {
            uint64_t aligned_addr = addr & ~(tlb.get_size() - 1);
            uint64_t offset = addr & (tlb.get_size() - 1);
            size_t chunk_size = std::min(len, tlb.get_size() - offset);
            uint8_t* src_ptr = (uint8_t*)tlb.get_mmio() + offset;

            tlb.map(x, y, aligned_addr);
            memcpy(dst_ptr, src_ptr, chunk_size);

            dst_ptr += chunk_size;
            len -= chunk_size;
            addr += chunk_size;
        }
    }

    static void noc_write(TlbWindow& tlb, uint8_t x, uint8_t y, uint64_t addr, const void* src, size_t len)
    {
        if (addr % 4 != 0 || len % 4 != 0) {
            throw std::invalid_argument("Misaligned");
        }

        const uint8_t* src_ptr = (const uint8_t*)src;

        while (len > 0) {
            uint64_t aligned_addr = addr & ~(tlb.get_size() - 1);
            uint64_t offset = addr & (tlb.get_size() - 1);
            size_t chunk_size = std::min(len, tlb.get_size() - offset);
            uint8_t* dst_ptr = (uint8_t*)tlb.get_mmio() + offset;

            tlb.map(x, y, aligned_addr);
            memcpy(dst_ptr, src_ptr, chunk_size);

            src_ptr += chunk_size;
            len -= chunk_size;
            addr += chunk_size;
        }
    }
};

class DmaBuffer
{
    Device& device;
    tt_dma_t* dma;
    void* mem;
    size_t len;
    uint64_t iova;
    uint64_t noc_addr;

public:
    DmaBuffer(Device& device, size_t len, int flags = TT_DMA_FLAG_NOC)
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

        int r = tt_dma_map(device.handle(), mem, len, flags, &dma);
        if (r) {
            munmap(mem, len);
            throw std::system_error(-r, std::generic_category(), "Failed to map DMA buffer");
        }

        r = tt_dma_get_dma_addr(dma, &iova);
        if (r) {
            tt_dma_unmap(device.handle(), dma);
            munmap(mem, len);
            throw std::system_error(-r, std::generic_category(), "Failed to get DMA address");
        }

        r = tt_dma_get_noc_addr(dma, &noc_addr);
        if ((flags & (TT_DMA_FLAG_NOC | TT_DMA_FLAG_NOC_TOP_DOWN)) && r) {
            tt_dma_unmap(device.handle(), dma);
            munmap(mem, len);
            throw std::system_error(-r, std::generic_category(), "Failed to get NOC address");
        }
    }

    void* get_mem() { return mem; }
    uint64_t get_iova() const { return iova; }
    uint64_t get_noc_addr() const { return noc_addr; }
    size_t get_len() const { return len; }

    ~DmaBuffer()
    {
        munmap(mem, len);
        tt_dma_unmap(device.handle(), dma);
    }

private:
    DmaBuffer(const DmaBuffer&) = delete;
    DmaBuffer& operator=(const DmaBuffer&) = delete;
    DmaBuffer(DmaBuffer&&) = delete;
    DmaBuffer& operator=(DmaBuffer&&) = delete;
};



} // namespace tt
