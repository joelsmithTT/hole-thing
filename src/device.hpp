// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
// SPDX-License-Identifier: GPL-2.0-only

#pragma once

#include "tlb.hpp"
#include "utility.hpp"
#include <cstdint>
#include <memory>
#include <vector>

#include <fcntl.h>
#include <linux/mman.h>
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
    size_t dmabuf_count{0};

public:
    static std::vector<std::string> enumerate_devices();

    Device(const std::string& chardev_path);

    bool iommu_enabled() const;
    int get_fd() const;

    bool is_wormhole() const;
    bool is_blackhole() const;
    bool is_translated();

    PciDeviceInfo get_device_info() const;
    MappedMemory& get_bar2();
    MappedMemory get_bar4();

    coord_t get_pcie_coordinates();
    coord_t get_noc_grid_size() const;

    void* allocate_dma_buffer(size_t size, uint64_t* iova_out, uint64_t* noc_addr_out);
    void free_dma_buffer(void* buffer, size_t size);

    void* pin_dma_buffer(void* buffer, size_t size);
    void unpin_dma_buffer(void* buffer, size_t size);

    std::unique_ptr<TlbWindow> map_tlb(uint16_t x, uint16_t y, uint64_t address, CacheMode mode, size_t size, int noc);
    std::unique_ptr<TlbWindow> map_tlb_2M(uint16_t x, uint16_t y, uint64_t address, CacheMode mode, int noc = 0);
    std::unique_ptr<TlbWindow> map_tlb_4G(uint16_t x, uint16_t y, uint64_t address, CacheMode mode, int noc = 0);

    void write_block(uint16_t x, uint16_t y, uint64_t address, const void* src, size_t size, int noc = 0);

    void noc_write32(uint16_t x, uint16_t y, uint64_t address, uint32_t value, int noc = 0);
    uint32_t noc_read32(uint16_t x, uint16_t y, uint64_t address, int noc = 0);

    uint64_t map_for_dma(void* buffer, size_t size, uint64_t* noc_addr_out = nullptr);
    void unmap_for_dma(void* buffer, size_t size);

    void enable_dbi(bool enable);

    ~Device() noexcept;
};

class KmdDmaBuffer
{
    Device& device;
    void* buffer;
    size_t size;
    uint64_t iova;
    uint64_t noc_addr;

public:
    KmdDmaBuffer(Device& device, size_t size)
        : device(device)
        , buffer(nullptr)
        , size(size)
        , iova(~0ULL)
        , noc_addr(~0ULL)
    {
        buffer = device.allocate_dma_buffer(size, &iova, &noc_addr);
        LOG_INFO("Allocated DMA buffer at VA %p; IOVA %lx; NOC addr %lx", buffer, iova, noc_addr);
    }

    void* get_buffer() const { return buffer; }
    size_t get_size() const { return size; }
    uint64_t get_iova() const { return iova; }
    uint64_t get_noc_addr() const { return noc_addr; }

    ~KmdDmaBuffer()
    {
        // These things live until the Device instance is destroyed.
    }
};

class DmaMappedMemory
{
    Device& device;
    void* buffer;
    size_t size;
    uint64_t iova;
    uint64_t noc_addr;

public:
    DmaMappedMemory(Device& device, void* buffer, size_t size)
        : device(device)
        , buffer(buffer)
        , size(size)
        , iova(~0ULL)
        , noc_addr(~0ULL)
    {
        iova = device.map_for_dma(buffer, size, &noc_addr);
    }

    void* get_buffer() const { return buffer; }
    size_t get_size() const { return size; }
    uint64_t get_iova() const { return iova; }
    uint64_t get_noc_addr() const { return noc_addr; }

    ~DmaMappedMemory()
    {
        device.unmap_for_dma(buffer, size);
    }
};

class UserDmaBuffer
{
    void* buffer;
    size_t size;
    DmaMappedMemory* mapped_memory;

public:
    UserDmaBuffer(Device& device, size_t size)
        : buffer(mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0))
        , size(size)
    {
        if (buffer == MAP_FAILED) {
            LOG_FATAL("Failed to mmap buffer");
        }
        mapped_memory = new DmaMappedMemory(device, buffer, size);
    }

    void* get_buffer() const { return buffer; }
    size_t get_size() const { return size; }
    uint64_t get_iova() const { return mapped_memory->get_iova(); }
    uint64_t get_noc_addr() const { return mapped_memory->get_noc_addr(); }

    ~UserDmaBuffer()
    {
        delete mapped_memory;
        munmap(buffer, size);
    }
};

class HugeDmaBuffer
{
    void* buffer;
    size_t size;
    DmaMappedMemory* mapped_memory;

public:
    HugeDmaBuffer(Device& device, size_t size)
        : buffer(mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE | MAP_HUGETLB | MAP_HUGE_1GB, -1, 0))
        , size(size)
    {
        if (buffer == MAP_FAILED) {
            LOG_FATAL("Failed to mmap buffer");
        }
        mapped_memory = new DmaMappedMemory(device, buffer, size);
    }

    void* get_buffer() const { return buffer; }
    size_t get_size() const { return size; }
    uint64_t get_iova() const { return mapped_memory->get_iova(); }
    uint64_t get_noc_addr() const { return mapped_memory->get_noc_addr(); }

    ~HugeDmaBuffer()
    {
        delete mapped_memory;
        munmap(buffer, size);
    }
};