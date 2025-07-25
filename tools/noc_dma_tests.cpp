// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
// SPDX-License-Identifier: GPL-2.0-only

#include "device.hpp"
#include "ioctl.h"
#include "logger.hpp"
#include "utility.hpp"
#include <linux/mman.h>

#include <iostream>
#include <unistd.h>

void test_noc_dma(Device& device, size_t num_buffers)
{
    int fd = device.get_fd();
    auto page_size = getpagesize();
    auto [x, y] = device.get_pcie_coordinates();

    struct
    {
        tenstorrent_pin_pages_in in;
        tenstorrent_pin_pages_out_extended out;
    } pin{};
    pin.in.output_size_bytes = sizeof(pin.out);

    std::vector<uint8_t*> buffers;
    std::vector<std::vector<uint8_t>> patterns;

    // Use smaller buffers for Wormhole devices, where we are constrained to a
    // little (just shy of 4GB) aperture from NOC to host system bus.
    size_t buffer_size = device.is_wormhole() ? 0x1000 : 0x10000;

    for (size_t i = 0; i < num_buffers; i++) {
        size_t buffer_alignment = page_size;
        void* buffer = std::aligned_alloc(buffer_alignment, buffer_size);

        if (!buffer) {
            LOG_FATAL("Failed to allocate buffer");
        }

        pin.in.virtual_address = reinterpret_cast<uint64_t>(buffer);
        pin.in.size = buffer_size;
        pin.in.flags = TENSTORRENT_PIN_PAGES_NOC_DMA;

        if (i % 2 == 0) {
            pin.in.flags |= TENSTORRENT_PIN_PAGES_NOC_TOP_DOWN;
        }

        if (ioctl(fd, TENSTORRENT_IOCTL_PIN_PAGES, &pin) != 0) {
            LOG_FATAL("Failed to pin pages");
        }

        uint64_t iova = pin.out.physical_address;
        uint64_t noc_addr = pin.out.noc_address;

        LOG_INFO("Buffer %zu: iova = %lx, noc_addr = %lx size = %zu", i, iova, noc_addr, buffer_size);

        std::vector<uint8_t> random_data(buffer_size);
        fill_with_random_data(random_data.data(), buffer_size);
        LOG_INFO("Writing to x=%u, y=%u, noc_addr=0x%lx", x, y, noc_addr);
        device.write_block(x, y, noc_addr, random_data.data(), buffer_size, 1);

        patterns.push_back(random_data);
        buffers.push_back(static_cast<uint8_t*>(buffer));

        if (device.iommu_enabled()) {
            buffer_size *= 2;
        }
    }

    for (size_t i = 0; i < num_buffers; i++) {
        auto buffer = buffers[i];
        auto pattern = patterns[i];

        // compare the pattern to the buffer
        auto* buf = static_cast<uint8_t*>(buffer);
        for (size_t j = 0; j < pattern.size(); j++) {
            int a = buf[j];
            int b = pattern[j];
            if (a != b) {
                LOG_FATAL("Buffer %zu mismatch at offset %zu: %d != %d", i, j, a, b);
            }
        }
        LOG_INFO("Buffer %zu: %zu bytes match", i, pattern.size());
    }

    for (size_t i = 0; i < num_buffers; i++) {
        tenstorrent_unpin_pages unpin{};
        unpin.in.virtual_address = reinterpret_cast<uint64_t>(buffers[i]);
        unpin.in.size = patterns[i].size();
        if (ioctl(fd, TENSTORRENT_IOCTL_UNPIN_PAGES, &unpin) != 0) {
            LOG_FATAL("Failed to unpin pages");
        }
    }
}

void test_noc_dma_with_dmabufs(const std::string& device_path, size_t num_buffers)
{
    Device device(device_path);
    int fd = device.get_fd();
    auto [x, y] = device.get_pcie_coordinates();

    for (size_t i = 0; i < num_buffers; i++) {
        tenstorrent_allocate_dma_buf dmabuf{};
        dmabuf.in.requested_size = 1 << 20;
        dmabuf.in.flags = TENSTORRENT_ALLOCATE_DMA_BUF_NOC_DMA;
        dmabuf.in.buf_index = (uint8_t)i;
        if (ioctl(fd, TENSTORRENT_IOCTL_ALLOCATE_DMA_BUF, &dmabuf) < 0) {
            SYSTEM_ERROR("Failed to allocate dmabuf");
        }

        void *mapping = mmap(nullptr, dmabuf.out.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, dmabuf.out.mapping_offset);
        if (mapping == MAP_FAILED) {
            SYSTEM_ERROR("Failed to mmap dmabuf");
        }

        uint64_t noc_addr = dmabuf.out.noc_address;
        uint64_t iova = dmabuf.out.physical_address;

        if (device.is_wormhole() && iova & 0xffff'ffff'0000'0000ULL) {
            // Not broken, per se, but legacy constraints force 32-bit DMA
            // addressing for ALLOCATE_DMA_BUF.  The intent here is to make sure
            // that I don't accidentaly break that.
            LOG_FATAL("DMA buffer IOVA is not 32-bit on Wormhole, this is broken.");
        }

        LOG_INFO("DMA buffer: noc_addr = %lx, iova = %lx, size = %zu", noc_addr, iova, dmabuf.out.size);

        // Write a pattern to the buffer using the device.
        std::vector<uint8_t> random_data(dmabuf.out.size);
        fill_with_random_data(random_data.data(), dmabuf.out.size);
        device.write_block(x, y, noc_addr, random_data.data(), dmabuf.out.size);

        for (size_t i = 0; i < dmabuf.out.size; i++) {
            if (random_data[i] != static_cast<uint8_t*>(mapping)[i]) {
                LOG_FATAL("Buffer mismatch at offset %zu", i);
            }
        }

        munmap(mapping, dmabuf.out.size);
    }
}

void test_noc_dma_hp(Device& device)
{
    int fd = device.get_fd();
    auto page_size = getpagesize();
    auto [x, y] = device.get_pcie_coordinates();

    struct
    {
        tenstorrent_pin_pages_in in;
        tenstorrent_pin_pages_out_extended out;
    } pin{};
    pin.in.output_size_bytes = sizeof(pin.out);

    std::vector<uint8_t*> buffers;
    std::vector<std::vector<uint8_t>> patterns;

    size_t buffer_size = 1 << 30;
    int32_t n = get_number_of_hugepages_free();

    n = device.is_wormhole() ? std::min(n, 4) : n;

    for (int32_t i = 0; i < n; i++) {
        if (device.is_wormhole() && i == 3) {
            buffer_size -= (1 << 28);
        }
        void* buffer = mmap(nullptr, buffer_size, PROT_READ | PROT_WRITE,
                            MAP_ANONYMOUS | MAP_PRIVATE | MAP_HUGETLB | MAP_HUGE_1GB, -1, 0);

        if (buffer == MAP_FAILED) {
            LOG_FATAL("Failed to allocate 1G hugepage");
        }

        pin.in.virtual_address = reinterpret_cast<uint64_t>(buffer);
        pin.in.size = buffer_size;
        pin.in.flags = TENSTORRENT_PIN_PAGES_NOC_DMA | TENSTORRENT_PIN_PAGES_CONTIGUOUS;

        if (i % 2 == 0) {
            pin.in.flags |= TENSTORRENT_PIN_PAGES_NOC_TOP_DOWN;
        }

        if (ioctl(fd, TENSTORRENT_IOCTL_PIN_PAGES, &pin) != 0) {
            LOG_FATAL("Failed to pin pages");
        }

        uint64_t iova = pin.out.physical_address;
        uint64_t noc_addr = pin.out.noc_address;

        LOG_INFO("Buffer %zu: iova = %lx, noc_addr = %lx", i, iova, noc_addr);

        std::vector<uint8_t> random_data(buffer_size);
        fill_with_random_data(random_data.data(), buffer_size);
        device.write_block(x, y, noc_addr, random_data.data(), buffer_size);

        patterns.push_back(random_data);
        buffers.push_back(static_cast<uint8_t*>(buffer));
    }

    for (size_t i = 0; i < buffers.size(); i++) {
        auto buffer = buffers[i];
        auto pattern = patterns[i];

        // compare the pattern to the buffer
        auto* buf = static_cast<uint8_t*>(buffer);
        for (size_t j = 0; j < pattern.size(); j++) {
            if (buf[j] != pattern[j]) {
                LOG_FATAL("Buffer %zu mismatch at offset %zu", i, j);
            }
        }
        LOG_INFO("Buffer %zu: %zu bytes match", i, pattern.size());
    }

    for (size_t i = 0; i < buffers.size(); i++) {
        tenstorrent_unpin_pages unpin{};
        unpin.in.virtual_address = reinterpret_cast<uint64_t>(buffers[i]);
        unpin.in.size = patterns[i].size();
        if (ioctl(fd, TENSTORRENT_IOCTL_UNPIN_PAGES, &unpin) != 0) {
            LOG_FATAL("Failed to unpin pages");
        }
        munmap(buffers[i], buffer_size);
    }
}

int main()
{
    for (auto device_path : Device::enumerate_devices()) {
        Device device(device_path);
        if (device.is_blackhole()) {
            LOG_INFO("Skipping blackhole device %s", device_path.c_str());
            continue;
        }
        // test_noc_dma_with_dmabufs(device_path, 16);
        if (device.iommu_enabled()) {
            test_noc_dma(device, 16);
        } else {
            test_noc_dma_hp(device);
        }
    }
    return 0;
}
