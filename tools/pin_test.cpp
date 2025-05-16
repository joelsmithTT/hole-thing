#include "ioctl.h"
#include "device.hpp"

#include <linux/mman.h>
#include <unistd.h>
#include <iostream>
void pin_hugepages_greedy(int fd)
{
    tenstorrent_pin_pages pin{};
    pin.in.output_size_bytes = sizeof(pin.out);

    std::vector<uint8_t*> buffers;
    std::vector<std::vector<uint8_t>> patterns;

    size_t buffer_size = 0x1000;

    void* buffer = mmap(nullptr, buffer_size, PROT_READ | PROT_WRITE,
                        MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);

    if (buffer == MAP_FAILED) {
        LOG_FATAL("Failed to allocate 1G hugepage");
    }


    pin.in.virtual_address = reinterpret_cast<uint64_t>(buffer);
    pin.in.size = buffer_size;
    pin.in.flags = TENSTORRENT_PIN_PAGES_CONTIGUOUS;

    if (ioctl(fd, TENSTORRENT_IOCTL_PIN_PAGES, &pin) != 0) {
        LOG_FATAL("Failed to pin pages (buffer_size = %zu)", buffer_size);
    }

    uint64_t iova = pin.out.physical_address;

    close(fd);

    std::cin >> iova;
}

int main()
{
    for (auto device_path : Device::enumerate_devices()) {
        int fd = open(device_path.c_str(), O_RDWR | O_CLOEXEC);
        if (fd == -1) {
            LOG_FATAL("Failed to open device %s", device_path.c_str());
        }
        pin_hugepages_greedy(fd);
    }
    return 0;
}