#include "holething.hpp"
#include <tenstorrent/ttkmd.h>

#include <iostream>
#include <string>

using namespace tt;

int blackhole_noc_sanity_check(Device& device)
{
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

int wormhole_noc_sanity_check(Device& device)
{
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

int noc_sanity_check(Device& device)
{
    if (device.is_blackhole())
        return blackhole_noc_sanity_check(device);
    else if (device.is_wormhole())
        return wormhole_noc_sanity_check(device);
    return -1;
}

int main(int argc, char *argv[])
{
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <device_id>\n", argv[0]);
        exit(1);
    }

    std::string device_path = "/dev/tenstorrent/" + std::string(argv[1]);

    Device device(device_path.c_str());

    if (noc_sanity_check(device) != 0) {
        fprintf(stderr, "%s: NOC already looks hung\n", device.get_path().c_str());
        return 1;
    }

    TlbWindow tlb1(device, 1ULL << 21, TT_MMIO_CACHE_MODE_UC);

    for (int x = 0; x < 32; ++x) {
        for (int y = 0; y < 32; ++y) {
            tlb1.map(x, y, 0x0);
            auto v = tlb1.read32(0);
            if (v == 0xffffffff) {
                goto out;
            }
        }
    }
out:
    if (noc_sanity_check(device) == 0) {
        fprintf(stderr, "Failed to hang the NOC\n");
        return 1;
    }

    printf("NOC successfully hung\n");
    printf("You probably want to reset the device now.\n");

    return 1;
}
