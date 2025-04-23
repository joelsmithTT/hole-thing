#include "device.hpp"
#include "logger.hpp"
#include "types.hpp"
#include "iatu.hpp"

void wormhole_noc_sanity_test()
{
    Wormhole device("/dev/tenstorrent/0");
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

#define TENSTORRENT_IOCTL_CONFIGURE_ATU		_IO(TENSTORRENT_IOCTL_MAGIC, 99)
struct tenstorrent_configure_atu_in {
	__u64 base;
	__u64 limit;
	__u64 target;
	__u64 reserved[2];
};

struct tenstorrent_configure_atu_out {};

struct tenstorrent_configure_atu {
	struct tenstorrent_configure_atu_in in;
	struct tenstorrent_configure_atu_out out;
};

void wormhole_noc_dma()
{
    Wormhole device("/dev/tenstorrent/0");
    int fd = device.get_fd();
    auto [x, y] = device.get_pcie_coordinates();
    size_t buffer_size = 0x2000;
    size_t buffer_alignment = 0x1000;
    void* buffer = std::aligned_alloc(buffer_alignment, buffer_size);

    if (!buffer) {
        LOG_ERROR("Failed to allocate buffer");
        return;
    }
    DEFER {
        std::free(buffer);
    };

    auto iova = device.map_for_dma(buffer, buffer_size);

    struct tenstorrent_configure_atu atu{};
    atu.in.base = 0x0;
    atu.in.limit = buffer_size - 1;
    atu.in.target = iova;
    if (ioctl(fd, TENSTORRENT_IOCTL_CONFIGURE_ATU, &atu) != 0) {
        RUNTIME_ERROR("Failed to configure ATU, did you hack tt-kmd to support this?");
    } else {
        LOG_INFO("Configured ATU: base: 0x%lx, limit: 0x%lx, target: 0x%lx", atu.in.base, atu.in.limit, atu.in.target);
    }

    auto noc = 0x8'0000'0000ULL;
    auto window = device.map_tlb_2M(x, y, noc, CacheMode::Uncached);
    LOG_INFO("IOVA: 0x%lx, NOC: 0x%lx", iova, noc);

    wh_iatu_debug_print(device);

    for (size_t i = 0; i < buffer_size; i += 4) {
        window->write32(i, i);
        auto value = window->read32(i);
        LOG_INFO("addr=%x value: %u", i, value);
        if (value == 0xffffffff) break;
    }
    return;

    for (size_t i = 0; i < buffer_size; i += 4) {
        uint32_t value_readback = window->read32(i);
        uint32_t value = *reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(buffer) + i);
        if (value_readback != i) {
            LOG_ERROR("Mismatch at offset %zu: expected %u, got %u", i, static_cast<uint32_t>(i), value_readback);
            RUNTIME_ERROR("DMA test failed");
        }
        if (value != i) {
            LOG_ERROR("Mismatch at offset %zu: expected %u, got %u", i, static_cast<uint32_t>(i), value);
            RUNTIME_ERROR("DMA test failed");
        }
    }
}

int main()
{
    wormhole_noc_sanity_test();
    wormhole_noc_dma();
    return 0;
}