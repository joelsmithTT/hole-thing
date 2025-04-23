#include "device.hpp"
#include "logger.hpp"

void blackhole_noc_sanity_check()
{
    static constexpr uint32_t BH_GRID_X = 17;
    static constexpr uint32_t BH_GRID_Y = 12;
    static constexpr uint64_t NOC_NODE_ID = 0xffb20044ULL;

    Blackhole device("/dev/tenstorrent/0");

    auto is_tensix = [](uint32_t x, uint32_t y) -> bool {
        return (y >= 2 && y <= 11) &&   // Valid y range
            ((x >= 1 && x <= 7) ||      // Left block
            (x >= 10 && x <= 16));      // Right block
    };

    for (uint32_t x = 0; x < BH_GRID_X; ++x) {
        for (uint32_t y = 0; y < BH_GRID_Y; ++y) {

            if (!is_tensix(x, y))
                continue;

            uint32_t node_id = device.noc_read32(x, y, NOC_NODE_ID);
            uint32_t node_id_x = (node_id >> 0x0) & 0x3f;
            uint32_t node_id_y = (node_id >> 0x6) & 0x3f;

            if (node_id_x != x || node_id_y != y) {
                LOG_ERROR("Node ID mismatch at (%d, %d): expected (%d, %d), got (%d, %d)\n",
                       x, y, x, y, node_id_x, node_id_y);
                RUNTIME_ERROR("Something is screwed up");
            }
        }
    }
}

int main()
{
    blackhole_noc_sanity_check();
    return 0;
}