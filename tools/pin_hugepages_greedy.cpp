// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
// SPDX-License-Identifier: GPL-2.0-only

#include "ioctl.h"
#include "device.hpp"

#include <linux/mman.h>
#include <unistd.h>

void pin_hugepages_greedy(Device& device)
{
    int fd = device.get_fd();

    struct
    {
        tenstorrent_pin_pages_in in;
        tenstorrent_pin_pages_out_extended out;
    } pin{};
    pin.in.output_size_bytes = sizeof(pin.out);

    std::vector<uint8_t*> buffers;
    std::vector<std::vector<uint8_t>> patterns;

    size_t buffer_size = 1 << 30;
    int n = get_number_of_hugepages_free();


    for (int i = 0; i < n; i++) {
        void* buffer = mmap(nullptr, buffer_size, PROT_READ | PROT_WRITE,
                            MAP_ANONYMOUS | MAP_PRIVATE | MAP_HUGETLB | MAP_HUGE_1GB, -1, 0);

        if (buffer == MAP_FAILED) {
            LOG_FATAL("Failed to allocate 1G hugepage");
        }

        if (i == 3) buffer_size -= 0x20000;

        pin.in.virtual_address = reinterpret_cast<uint64_t>(buffer);
        pin.in.size = buffer_size;
        pin.in.flags = TENSTORRENT_PIN_PAGES_NOC_DMA | TENSTORRENT_PIN_PAGES_CONTIGUOUS;

        if (ioctl(fd, TENSTORRENT_IOCTL_PIN_PAGES, &pin) != 0) {
            LOG_FATAL("Failed to pin pages (buffer_size = %zu; i = %d)", buffer_size, i);
        }

        uint64_t iova = pin.out.physical_address;
        uint64_t noc_addr = pin.out.noc_address;

        LOG_INFO("Buffer %zu: iova = %lx, noc_addr = %lx", i, iova, noc_addr);

        uint8_t* buf = static_cast<uint8_t*>(buffer);

        
    }
}

int main()
{
    for (auto device_path : Device::enumerate_devices()) {
        Device device(device_path);
        pin_hugepages_greedy(device);
    }
    return 0;
}