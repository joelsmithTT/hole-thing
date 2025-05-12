#include "device.hpp"
#include "ioctl.h"
#include "logger.hpp"
#include "utility.hpp"
#include <linux/mman.h>

#include <unistd.h>
#include <deque>

int get_number_of_hugepages(Device& device)
{
    std::string hugepage_path = "/sys/kernel/mm/hugepages/hugepages-1048576kB/free_hugepages";
    auto n = read_small_file<int>(hugepage_path);
    auto available_hugepages = n.value_or(0);

    LOG_INFO("Available 1G hugepages: %d", available_hugepages);
    return available_hugepages;
}

void test_noc_dma(Device& device,size_t num_buffers)
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

    size_t buffer_size = 0x8000;

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
            pin.in.flags |= TENSTORRENT_PIN_PAGES_ATU_TOP_DOWN;
        }

        if (ioctl(fd, TENSTORRENT_IOCTL_PIN_PAGES, &pin) != 0) {
            LOG_FATAL("Failed to pin pages");
        }

        uint64_t iova = pin.out.physical_address;
        uint64_t noc_addr = pin.out.noc_address;

        LOG_INFO("Buffer %zu: iova = %lx, noc_addr = %lx", i, iova, noc_addr);

        auto random_data = random_vec<uint8_t>(buffer_size);
        device.write_block(x, y, noc_addr, random_data.data(), buffer_size);

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
            if (buf[j] != pattern[j]) {
                LOG_FATAL("Buffer %zu mismatch at offset %zu", i, j);
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

size_t random_size_4k_multiple(size_t max_size)
{
    static thread_local std::mt19937 gen(std::random_device{}());
    size_t num_blocks = max_size / 4096;
    if (num_blocks <= 1) {
        return 4096;
    }
    std::uniform_int_distribution<size_t> dist(1, num_blocks - 1);
    return dist(gen) * 4096;
}

void test_noc_dma2(Device& device,size_t num_buffers)
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

    std::deque<uint8_t*> buffers;
    std::deque<std::vector<uint8_t>> patterns;

    for (size_t n = 0; n < 1024; ++n) {
        if (buffers.size() >= num_buffers) {
            auto buffer = buffers.front();
            auto pattern = patterns.front();
            munmap(buffer, pattern.size());

            tenstorrent_unpin_pages unpin{};
            unpin.in.virtual_address = reinterpret_cast<uint64_t>(buffer);
            unpin.in.size = pattern.size();
            if (ioctl(fd, TENSTORRENT_IOCTL_UNPIN_PAGES, &unpin) != 0) {
                LOG_FATAL("Failed to unpin pages");
            }

            buffers.pop_front();
            patterns.pop_front();
        }

        auto size = random_size_4k_multiple((1 << 28) - 0x8000);
        auto buffer = std::aligned_alloc(page_size, size);


        if (!buffer) {
            LOG_FATAL("Failed to allocate buffer");
        }

        pin.in.virtual_address = reinterpret_cast<uint64_t>(buffer);
        pin.in.size = size;
        pin.in.flags = TENSTORRENT_PIN_PAGES_NOC_DMA;

        if (n % 2 == 0) {
            pin.in.flags |= TENSTORRENT_PIN_PAGES_ATU_TOP_DOWN;
        }

        if (ioctl(fd, TENSTORRENT_IOCTL_PIN_PAGES, &pin) != 0) {
            LOG_ERROR("Failed to pin pages %s; size = %zu", strerror(errno), size);
            std::free(buffer);

            size_t total_size = 0;
            for (size_t i = 0; i < buffers.size(); i++) {
                total_size += patterns[i].size();
            }
            LOG_INFO("Total size = %zu", total_size);

            LOG_FATAL("DONE");
        }

        uint64_t iova = pin.out.physical_address;
        uint64_t noc_addr = pin.out.noc_address;

        LOG_INFO("Buffer %zu: size = %zu iova = %lx noc_addr = %lx", n, size, iova, noc_addr);
        
        auto random_data = random_vec<uint8_t>(size);
        device.write_block(x, y, noc_addr, random_data.data(), size);

        patterns.push_back(random_data);
        buffers.push_back(static_cast<uint8_t*>(buffer));

        for (size_t i = 0; i < num_buffers; i++) {
            auto buffer = buffers[i];
            auto pattern = patterns[i];

            // compare the pattern to the buffer
            auto* buf = static_cast<uint8_t*>(buffer);
            for (size_t j = 0; j < pattern.size(); j++) {
                if (buf[j] != pattern[j]) {
                    LOG_FATAL("Buffer %zu mismatch at offset %zu", i, j);
                }
            }
            // LOG_INFO("Buffer %zu: %zu bytes match", i, pattern.size());
        }
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
    int n = get_number_of_hugepages(device);

    n = std::min(n, 3);

    for (int i = 0; i < n; i++) {
        void* buffer = mmap(nullptr, buffer_size, PROT_READ | PROT_WRITE,
                            MAP_ANONYMOUS | MAP_PRIVATE | MAP_HUGETLB | MAP_HUGE_1GB, -1, 0);

        if (buffer == MAP_FAILED) {
            LOG_FATAL("Failed to allocate 1G hugepage");
        }

        pin.in.virtual_address = reinterpret_cast<uint64_t>(buffer);
        pin.in.size = buffer_size;
        pin.in.flags = TENSTORRENT_PIN_PAGES_NOC_DMA | TENSTORRENT_PIN_PAGES_CONTIGUOUS;

        if (i % 2 == 0) {
            pin.in.flags |= TENSTORRENT_PIN_PAGES_ATU_TOP_DOWN;
        }

        if (ioctl(fd, TENSTORRENT_IOCTL_PIN_PAGES, &pin) != 0) {
            LOG_FATAL("Failed to pin pages");
        }

        uint64_t iova = pin.out.physical_address;
        uint64_t noc_addr = pin.out.noc_address;

        LOG_INFO("Buffer %zu: iova = %lx, noc_addr = %lx", i, iova, noc_addr);

        auto random_data = random_vec<uint8_t>(buffer_size);
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
        if (device.iommu_enabled()) {
            test_noc_dma2(device, 16);
        } else {
            test_noc_dma_hp(device);
        }
    }
    return 0;
}