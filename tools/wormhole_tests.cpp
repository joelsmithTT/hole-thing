#include "device.hpp"
#include "logger.hpp"
#include "types.hpp"

void wormhole_noc_sanity_test()
{
    Device device("/dev/tenstorrent/0");
    {
        constexpr uint32_t ARC_X = 0;
        constexpr uint32_t ARC_Y = 10;
        constexpr uint64_t ARC_NOC_NODE_ID = 0xFFFB2002CULL;

        auto node_id = device.noc_read32(ARC_X, ARC_Y, ARC_NOC_NODE_ID);
        auto x = (node_id >> 0x0) & 0x3f;
        auto y = (node_id >> 0x6) & 0x3f;
        if (x != ARC_X || y != ARC_Y) {
            LOG_ERROR("Expected x: %u, y: %u, got x: %u, y: %u", ARC_X, ARC_Y, x, y);
            RUNTIME_ERROR("Something is screwed up with the chip");
        } else {
            LOG_INFO("ARC node_id: %08x", node_id);
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
            LOG_ERROR("Expected x: %u, y: %u, got x: %u, y: %u", DDR_X, DDR_Y, x, y);
            RUNTIME_ERROR("Something is screwed up with the chip");
        } else {
            LOG_INFO("DDR node_id: %08x", node_id);
        }
    }

    LOG_INFO("Wormhole NOC sanity test PASSED");
}

#if 0
void wormhole_new_pin()
{
    Device device("/dev/tenstorrent/0");
    int fd = device.get_fd();
    auto [x, y] = device.get_pcie_coordinates();
    size_t buffer_size = 2ULL * (1024 * 1024 * 1024);
    size_t buffer_alignment = 0x1000;
    void* buffer = std::aligned_alloc(buffer_alignment, buffer_size);

    if (!buffer) {
        LOG_ERROR("Failed to allocate buffer");
        return;
    }
    DEFER
    {
        std::free(buffer);
    };

    struct
    {
        tenstorrent_pin_pages_in in;
        tenstorrent_pin_pages_out_extended out;
    } pin1{};
    pin1.in.output_size_bytes = sizeof(pin1.out);
    pin1.in.virtual_address = reinterpret_cast<uint64_t>(buffer);
    pin1.in.size = buffer_size;
    pin1.in.flags = TENSTORRENT_PIN_PAGES_NOC_DMA;

    if (ioctl(fd, TENSTORRENT_IOCTL_PIN_PAGES, &pin1) != 0) {
        throw std::system_error(errno, std::generic_category(), "Failed to pin pages");
    }

    uint64_t iova1 = pin1.out.physical_address;
    uint64_t noc_addr1 = pin1.out.noc_address;

    LOG_DEBUG("Mapped buffer at VA %p to IOVA 0x%016lx; NOC: 0x%08x", buffer, iova1, noc_addr1);

    size_t buffer_size2 = 0x1000;
    void* buffer2 = std::aligned_alloc(buffer_alignment, buffer_size2);
    if (!buffer2) {
        LOG_ERROR("Failed to allocate buffer");
        return;
    }
    DEFER
    {
        std::free(buffer2);
    };
    struct
    {
        tenstorrent_pin_pages_in in;
        tenstorrent_pin_pages_out_extended out;
    } pin2{};
    pin2.in.output_size_bytes = sizeof(pin2.out);
    pin2.in.virtual_address = reinterpret_cast<uint64_t>(buffer2);
    pin2.in.size = buffer_size2;
    pin2.in.flags = TENSTORRENT_PIN_PAGES_NOC_DMA | TENSTORRENT_PIN_PAGES_ATU_TOP_DOWN;

    if (ioctl(fd, TENSTORRENT_IOCTL_PIN_PAGES, &pin2) != 0) {
        throw std::system_error(errno, std::generic_category(), "Failed to pin pages");
    }

    uint64_t iova2 = pin2.out.physical_address;
    uint64_t noc_addr2 = pin2.out.noc_address;

    LOG_DEBUG("Mapped buffer at VA %p to IOVA 0x%016lx; NOC: 0x%08x", buffer2, iova2, noc_addr2);

    size_t buffer_size3 = 0x100000;
    void* buffer3 = std::aligned_alloc(buffer_alignment, buffer_size3);
    if (!buffer3) {
        LOG_ERROR("Failed to allocate buffer");
        return;
    }
    DEFER
    {
        std::free(buffer3);
    };
    struct
    {
        tenstorrent_pin_pages_in in;
        tenstorrent_pin_pages_out_extended out;
    } pin3{};
    pin3.in.output_size_bytes = sizeof(pin3.out);
    pin3.in.virtual_address = reinterpret_cast<uint64_t>(buffer3);
    pin3.in.size = buffer_size3;
    pin3.in.flags = TENSTORRENT_PIN_PAGES_NOC_DMA | TENSTORRENT_PIN_PAGES_ATU_TOP_DOWN;

    if (ioctl(fd, TENSTORRENT_IOCTL_PIN_PAGES, &pin3) != 0) {
        throw std::system_error(errno, std::generic_category(), "Failed to pin pages");
    }

    uint64_t iova3 = pin3.out.physical_address;
    uint64_t noc_addr3 = pin3.out.noc_address;

    LOG_DEBUG("Mapped buffer at VA %p to IOVA 0x%016lx; NOC: 0x%08x", buffer3, iova3, noc_addr3);
    tenstorrent_unpin_pages unpin{};
    unpin.in.virtual_address = reinterpret_cast<uint64_t>(buffer3);
    unpin.in.size = buffer_size3;
    if (ioctl(fd, TENSTORRENT_IOCTL_UNPIN_PAGES, &unpin)) {
        throw std::system_error(errno, std::generic_category(), "Failed to unpin pages");
    }

}

void wormhole_pin_unpin_test()
{
    Device device("/dev/tenstorrent/0");
    int fd = device.get_fd();

    constexpr size_t buffer_alignment = 4096;
    constexpr size_t num_buffers = 20; // More than 16 to test unpinning and repinning
    std::vector<void*> buffers;
    std::vector<size_t> buffer_sizes;
    std::vector<uint64_t> iovas;
    std::vector<uint64_t> noc_addrs;
    std::vector<bool> is_pinned;
    std::vector<bool> is_top_down;

    // Create buffers of various sizes
    for (size_t i = 0; i < num_buffers; i++) {
        // Vary buffer sizes from 4KB to 4MB
        size_t buffer_size = buffer_alignment * (1 + (i % 1024));
        void* buffer = std::aligned_alloc(buffer_alignment, buffer_size);
        if (!buffer) {
            LOG_ERROR("Failed to allocate buffer %zu", i);
            continue;
        }

        // Initialize buffer with some pattern
        memset(buffer, i & 0xFF, buffer_size);

        buffers.push_back(buffer);
        buffer_sizes.push_back(buffer_size);
        iovas.push_back(0);
        noc_addrs.push_back(0);
        is_pinned.push_back(false);
        // Alternate between top-down and bottom-up
        is_top_down.push_back((i % 2) == 0);
    }

    // Cleanup handler for all buffers
    DEFER
    {
        for (void* buffer : buffers) {
            std::free(buffer);
        }
    };

    LOG_DEBUG("Created %zu buffers for testing", buffers.size());

    // First, pin all buffers
    for (size_t i = 0; i < buffers.size(); i++) {
        struct
        {
            tenstorrent_pin_pages_in in;
            tenstorrent_pin_pages_out_extended out;
        } pin{};

        pin.in.output_size_bytes = sizeof(pin.out);
        pin.in.virtual_address = reinterpret_cast<uint64_t>(buffers[i]);
        pin.in.size = buffer_sizes[i];
        pin.in.flags = TENSTORRENT_PIN_PAGES_NOC_DMA;

        if (is_top_down[i]) {
            pin.in.flags |= TENSTORRENT_PIN_PAGES_ATU_TOP_DOWN;
        }

        if (ioctl(fd, TENSTORRENT_IOCTL_PIN_PAGES, &pin) != 0) {
            if (errno == ENOSPC) {
                LOG_DEBUG("Buffer %zu: Expected failure - exceeded 16-region limit", i);
            } else {
                LOG_ERROR("Failed to pin buffer %zu: %s", i, strerror(errno));
            }
            continue;
        }

        iovas[i] = pin.out.physical_address;
        noc_addrs[i] = pin.out.noc_address;
        is_pinned[i] = true;

        LOG_DEBUG("Buffer %zu: VA %p, size 0x%016zx, IOVA 0x%016lx, NOC 0x%08x, %s", i, buffers[i], buffer_sizes[i],
                  iovas[i], noc_addrs[i], is_top_down[i] ? "top-down" : "bottom-up");
    }

    // Unpin some buffers (every third one)
    for (size_t i = 0; i < buffers.size(); i += 3) {
        if (!is_pinned[i])
            continue;

        tenstorrent_unpin_pages unpin{};
        unpin.in.virtual_address = reinterpret_cast<uint64_t>(buffers[i]);
        unpin.in.size = buffer_sizes[i];

        if (ioctl(fd, TENSTORRENT_IOCTL_UNPIN_PAGES, &unpin) != 0) {
            LOG_ERROR("Failed to unpin buffer %zu: %s", i, strerror(errno));
            continue;
        }

        LOG_DEBUG("Unpinned buffer %zu", i);
        is_pinned[i] = false;
    }

    // Re-pin the unpinned buffers with different sizes and opposite direction
    for (size_t i = 0; i < buffers.size(); i += 3) {
        if (is_pinned[i])
            continue;

        // Free the old buffer and allocate a new one with different size
        std::free(buffers[i]);
        size_t new_size = buffer_sizes[i] * 2; // Double the size
        void* new_buffer = std::aligned_alloc(buffer_alignment, new_size);
        if (!new_buffer) {
            LOG_ERROR("Failed to reallocate buffer %zu", i);
            continue;
        }

        memset(new_buffer, (i + 100) & 0xFF, new_size);
        buffers[i] = new_buffer;
        buffer_sizes[i] = new_size;

        // Flip the direction
        is_top_down[i] = !is_top_down[i];

        struct
        {
            tenstorrent_pin_pages_in in;
            tenstorrent_pin_pages_out_extended out;
        } pin{};

        pin.in.output_size_bytes = sizeof(pin.out);
        pin.in.virtual_address = reinterpret_cast<uint64_t>(buffers[i]);
        pin.in.size = buffer_sizes[i];
        pin.in.flags = TENSTORRENT_PIN_PAGES_NOC_DMA;

        if (is_top_down[i]) {
            pin.in.flags |= TENSTORRENT_PIN_PAGES_ATU_TOP_DOWN;
        }

        if (ioctl(fd, TENSTORRENT_IOCTL_PIN_PAGES, &pin) != 0) {
            if (errno == ENOSPC) {
                LOG_DEBUG("Buffer %zu: Expected failure - exceeded 16-region limit", i);
            } else {
                LOG_ERROR("Failed to re-pin buffer %zu: %s", i, strerror(errno));
            }
            continue;
        }

        iovas[i] = pin.out.physical_address;
        noc_addrs[i] = pin.out.noc_address;
        is_pinned[i] = true;

        LOG_DEBUG("Re-pinned buffer %zu: VA %p, new size 0x%016zx, IOVA 0x%016lx, NOC 0x%08x, %s", i, buffers[i],
                  buffer_sizes[i], iovas[i], noc_addrs[i], is_top_down[i] ? "top-down" : "bottom-up");
    }

    // Unpin all remaining buffers
    for (size_t i = 0; i < buffers.size(); i++) {
        if (!is_pinned[i])
            continue;

        tenstorrent_unpin_pages unpin{};
        unpin.in.virtual_address = reinterpret_cast<uint64_t>(buffers[i]);
        unpin.in.size = buffer_sizes[i];

        if (ioctl(fd, TENSTORRENT_IOCTL_UNPIN_PAGES, &unpin) != 0) {
            LOG_ERROR("Failed to unpin buffer %zu in final cleanup: %s", i, strerror(errno));
        } else {
            LOG_DEBUG("Unpinned buffer %zu in final cleanup", i);
            is_pinned[i] = false;
        }
    }

    LOG_DEBUG("Completed pin/unpin test");
}
#endif

int main()
{
    // wormhole_noc_sanity_test();
    // wormhole_noc_dma();
    // wormhole_new_pin();
    // wormhole_pin_unpin_test();
    return 0;
}
