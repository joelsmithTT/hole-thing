#include "device.hpp"
#include "logger.hpp"
#include "types.hpp"
#include <iostream>

void wormhole_noc_sanity_test(Device& device)
{
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

    constexpr uint64_t TENSIX_NOC_NODE_ID = 0xffb2002cULL;
    constexpr uint64_t TENSIX_NODE_ID_LOGICAL = 0xffb20138ULL;
    auto is_tensix_wh = [](uint32_t x, uint32_t y) -> bool {
        return (((y != 6) && (y >= 1) && (y <= 11)) && // valid Y
                ((x != 5) && (x >= 1) && (x <= 9)));   // valid X
    };
    for (uint32_t x = 0; x < 10; ++x) {
        for (uint32_t y = 0; y < 12; ++y) {
            if (!is_tensix_wh(x, y)) {
                continue;
            }
            auto node_id = device.noc_read32(x, y, TENSIX_NOC_NODE_ID);
            auto node_id_x = (node_id >> 0x0) & 0x3f;
            auto node_id_y = (node_id >> 0x6) & 0x3f;

            if (node_id_x != x || node_id_y != y) {
                LOG_ERROR("Expected x: %u, y: %u, got x: %u, y: %u", x, y, node_id_x, node_id_y);
                RUNTIME_ERROR("Something is screwed up with the chip");
            }
        }
    }

    LOG_INFO("Wormhole NOC sanity test PASSED");

    for (uint32_t x = 0; x < 10; ++x) {
        for (uint32_t y = 0; y < 12; ++y) {
            if (!is_tensix_wh(x, y)) {
                continue;
            }
            auto node_id = device.noc_read32(x, y, TENSIX_NOC_NODE_ID);
            auto node_id_x = (node_id >> 0x0) & 0x3f;
            auto node_id_y = (node_id >> 0x6) & 0x3f;

            auto node_id_logical = device.noc_read32(x, y, TENSIX_NODE_ID_LOGICAL);
            auto node_id_logical_x = (node_id_logical >> 0x0) & 0x3f;
            auto node_id_logical_y = (node_id_logical >> 0x6) & 0x3f;

            LOG_INFO("(x=%u, y=%u) -> (x=%u, y=%u)", node_id_x, node_id_y, node_id_logical_x, node_id_logical_y);
        }
    }
}

void wormhole_noc_poke_test(Device& device)
{
    #if 0
    static constexpr uint64_t NOC_NODE_ID = 0x1FD0202C;
    static constexpr uint64_t NOC_TRANS_T = 0x1fd02128;
    // static constexpr uint64_t NOC_NODE_id = 0xFFB2002C;
    // static constexpr uint64_t NOC_TTABLE_ = 0xFFB20128;
    auto bar0 = device.get_bar0();
    auto node_id = bar0.read32(NOC_NODE_ID);

    auto cols = (node_id >> 12) & 0b1111111;
    auto rows = (node_id >> 19) & 0b1111111;

    LOG_INFO("Wormhole NOC poke test: rows: %u, cols: %u", rows, cols);

    auto trans_t = bar0.read32(NOC_TRANS_T);
    LOG_INFO("Translation table: %08x", bar0.read32(NOC_TRANS_T));
    LOG_INFO("Translation table: %08x", bar0.read32(NOC_TRANS_T + 4));
    LOG_INFO("Translation table: %08x", bar0.read32(NOC_TRANS_T + 8));
    LOG_INFO("Translation table: %08x", bar0.read32(NOC_TRANS_T + 12));

    // Read a translation table.
    auto window = device.map_tlb_2M(1, 1, 0xFFB20118ULL, CacheMode::Uncached, 0);
    // LOG_INFO("Translation table: %08x", window->read32(0));
    // LOG_INFO("Translation table: %08x", window->read32(4));
    // LOG_INFO("Translation table: %08x", window->read32(8));
    // LOG_INFO("Translation table: %08x", window->read32(12));

    window = device.map_tlb_2M(1, 1, 0xFFB20128ULL, CacheMode::Uncached, 0);
    LOG_INFO("Translation table: %08x", window->read32(0));
    LOG_INFO("Translation table: %08x", window->read32(4));
    LOG_INFO("Translation table: %08x", window->read32(8));
    LOG_INFO("Translation table: %08x", window->read32(12));
    #endif

    auto window = device.map_tlb_2M(0, 0, 0, CacheMode::Uncached, 0);
    window->write32(0, 0x5555aaaa);
    LOG_INFO("Read: %08x", window->read32(0));

    window = device.map_tlb_2M(0, 28, 0, CacheMode::Uncached, 0);
    LOG_INFO("Read: %08x", window->read32(0));
    window = device.map_tlb_2M(0, 29, 0, CacheMode::Uncached, 0);
    LOG_INFO("Read: %08x", window->read32(0));
    window = device.map_tlb_2M(0, 30, 0, CacheMode::Uncached, 0);
    LOG_INFO("Read: %08x", window->read32(0));
    window = device.map_tlb_2M(0, 31, 0, CacheMode::Uncached, 0);
    LOG_INFO("Read: %08x", window->read32(0));
}

int main()
{
    for (auto device_path : Device::enumerate_devices()) {
        Device device(device_path);
        if (!device.is_wormhole()) {
            continue;
        }

        wormhole_noc_poke_test(device);
        // wormhole_noc_sanity_test(device);
    }
    return 0;
}