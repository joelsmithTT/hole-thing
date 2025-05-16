// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
// SPDX-License-Identifier: GPL-2.0-only

#pragma once

#include "tlb.hpp"
#include "utility.hpp"
#include <cstdint>
#include <memory>
#include <vector>

#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

static constexpr uint16_t WORMHOLE_ID = 0x401e;
static constexpr uint16_t BLACKHOLE_ID = 0xb140;

struct PciDeviceInfo
{
    uint16_t vendor_id;
    uint16_t device_id;
    uint16_t pci_domain;
    uint16_t pci_bus;
    uint16_t pci_device;
    uint16_t pci_function;
};

class Device
{
    int fd;
    MappedMemory bar2;
    PciDeviceInfo device_info;

public:
    static std::vector<std::string> enumerate_devices();

    Device(const std::string& chardev_path);

    bool iommu_enabled() const;

    int get_fd() const;

    bool is_wormhole() const;

    bool is_blackhole() const;

    PciDeviceInfo get_device_info() const;

    MappedMemory& get_bar2();

    MappedMemory get_bar4();

    coord_t get_pcie_coordinates();

    coord_t get_noc_grid_size() const;

    std::unique_ptr<TlbWindow> map_tlb(uint16_t x, uint16_t y, uint64_t address, CacheMode mode, size_t window_size,
                                       int noc = 0);

    std::unique_ptr<TlbWindow> map_tlb_2M(uint16_t x, uint16_t y, uint64_t address, CacheMode mode, int noc = 0);

    std::unique_ptr<TlbWindow> map_tlb_4G(uint16_t x, uint16_t y, uint64_t address, CacheMode mode);

    void write_block(uint16_t x, uint16_t y, uint64_t address, const void* src, size_t size);

    void noc_write32(uint16_t x, uint16_t y, uint64_t address, uint32_t value);

    uint32_t noc_read32(uint16_t x, uint16_t y, uint64_t address);

    uint64_t map_for_dma(void* buffer, size_t size);

    void unmap_for_dma(void* buffer, size_t size);

    void enable_dbi(bool enable);

    ~Device() noexcept;
};
