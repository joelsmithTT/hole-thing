#include "ttio.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ttio.hpp"

#include <algorithm>
#include <filesystem>
#include <map>
#include <random>

void blackhole_noc_sanity_check(tt::Device& device)
{
    static constexpr uint64_t NOC_NODE_ID_LOGICAL = 0xffb20148ULL;

    if (!device.is_blackhole())
        return;

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
                printf("Node ID mismatch, expected (%u, %u), got (%u, %u)\n", x, y, node_id_x, node_id_y);
                exit(1);
            }
        }
    }
    printf("Blackhole NOC sanity test PASSED\n");
}

void wormhole_noc_sanity_check(tt::Device& device)
{
    if (!device.is_wormhole())
        return;

    {
        constexpr uint32_t ARC_X = 0;
        constexpr uint32_t ARC_Y = 10;
        constexpr uint64_t ARC_NOC_NODE_ID = 0xFFFB2002CULL;

        auto node_id = device.noc_read32(ARC_X, ARC_Y, ARC_NOC_NODE_ID);
        auto x = (node_id >> 0x0) & 0x3f;
        auto y = (node_id >> 0x6) & 0x3f;
        if (x != ARC_X || y != ARC_Y) {
            printf("ARC node ID mismatch, expected (%u, %u), got (%u, %u)\n", ARC_X, ARC_Y, x, y);
            exit(1);
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
            printf("DDR node ID mismatch, expected (%u, %u), got (%u, %u)\n", DDR_X, DDR_Y, x, y);
            exit(1);
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
                printf("Expected x: %u, y: %u, got x: %u, y: %u\n", x, y, node_id_x, node_id_y);
            }
        }
    }

    printf("Wormhole NOC sanity test PASSED\n");
}

void fill_with_random_data(void* ptr, size_t bytes)
{
    if (bytes == 0)
        return;

    static std::mt19937_64 eng(std::random_device{}());

    size_t num_uint64 = bytes / sizeof(uint64_t);
    size_t rem_bytes = bytes % sizeof(uint64_t);

    uint64_t* ptr64 = static_cast<uint64_t*>(ptr);
    for (size_t i = 0; i < num_uint64; ++i) {
        ptr64[i] = eng();
    }

    if (rem_bytes > 0) {
        uint8_t* ptr8 = reinterpret_cast<uint8_t*>(ptr64 + num_uint64);
        uint64_t last_random_chunk = eng();
        unsigned char* random_bytes_for_remainder = reinterpret_cast<unsigned char*>(&last_random_chunk);
        for (size_t i = 0; i < rem_bytes; ++i) {
            ptr8[i] = random_bytes_for_remainder[i];
        }
    }
}

void test_noc_dma(tt::Device& device, size_t magnitude)
{
    size_t buffer_size = 1ULL << magnitude;
    tt::DmaBuffer buffer(device.handle(), buffer_size);
    uint8_t* data = buffer.get_mem<uint8_t>();
    uint64_t noc_addr = buffer.get_noc_addr();

    std::vector<uint8_t> pattern(buffer_size);
    fill_with_random_data(pattern.data(), buffer_size);

    auto [x, y] = device.get_pcie_coordinates();
    printf("Writing to x=%u, y=%u, noc_addr=0x%lx\n", x, y, noc_addr);
    device.noc_write(x, y, noc_addr, pattern.data(), buffer_size);

    if (memcmp(data, pattern.data(), buffer_size) != 0) {
        printf("Data mismatch\n");
        return;
    }

    printf("NOC DMA test PASSED (size=0x%lx)\n", buffer_size);
}

void test_telemetry(tt::Device& device)
{
    if (device.is_blackhole()) {
        std::map<const char*, uint32_t> telemetry_tags = {
            {" AI Clock (MHz)", 14},
            {"Fan Speed (RPM)", 41},
        };
        for (auto [name, tag] : telemetry_tags) {
            uint32_t telemetry = device.read_bh_telemetry(tag);
            printf("Blackhole telemetry: %s = %u\n", name, telemetry);
        }
    }
}


void run_tests(tt::Device& device)
{
    blackhole_noc_sanity_check(device);
    wormhole_noc_sanity_check(device);

    test_telemetry(device);

    test_noc_dma(device, 21);
    test_noc_dma(device, 28);
    test_noc_dma(device, 30);
}


std::vector<std::string> enumerate_devices()
{
    std::vector<std::string> devices;
    for (const auto& entry : std::filesystem::directory_iterator("/dev/tenstorrent/")) {
        if (std::filesystem::is_character_file(entry.path()) || std::filesystem::is_block_file(entry.path())) {
            devices.push_back(entry.path().string());
        }
    }

    std::sort(devices.begin(), devices.end());
    return devices;
}

int main()
{
    for (auto device_path : enumerate_devices()) {
        tt::Device device(device_path.c_str());
        run_tests(device);
    }

    return 0;
}
