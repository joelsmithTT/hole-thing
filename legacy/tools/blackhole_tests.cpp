#include "device.hpp"
#include "logger.hpp"
#include "types.hpp"

#include <array>
#include <algorithm>

void blackhole_noc_sanity_check(Device& device)
{
    static constexpr uint64_t NOC_NODE_ID = 0xffb20044ULL;
    static constexpr uint64_t NOC_NODE_ID_LOGICAL = 0xffb20148ULL;

    bool translated = device.is_translated();
    uint64_t address = translated ? NOC_NODE_ID_LOGICAL : NOC_NODE_ID;

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

            uint32_t node_id = device.noc_read32(x, y, address);
            uint32_t node_id_x = (node_id >> 0x0) & 0x3f;
            uint32_t node_id_y = (node_id >> 0x6) & 0x3f;

            if (node_id_x != x || node_id_y != y) {
                LOG_ERROR("Node ID mismatch at (%u, %u): expected (%u, %u), got (%u, %u) (translated=%d)",
                       x, y, x, y, node_id_x, node_id_y, translated);
                RUNTIME_ERROR("Something is screwed up");
            }
        }
    }
    LOG_INFO("NOC sanity check passed");
}


struct Blackhole {
    static constexpr size_t NOC_SIZE_X = 17;
    static constexpr size_t NOC_SIZE_Y = 12;
    static constexpr std::array<uint32_t, NOC_SIZE_X> NOC0_X = { 0, 1, 16, 2, 15, 3, 14, 4, 13, 5, 12, 6, 11, 7, 10, 8, 9 };
    static constexpr std::array<uint32_t, NOC_SIZE_Y> NOC0_Y = { 0, 1, 11, 2, 10, 3, 9, 4, 8, 5, 7, 6 };

#define D 0x00 // DDR
#define T 0x01 // TENSIX
#define E 0x02 // ETH
#define I 0x03 // IGNORE
#define L 0x04 // L2CPU
#define P 0x05 // PCIE
#define A 0x06 // ARC
#define X 0x07 // X
    static constexpr std::array<char, X + 1> C = { 'D', 'T', 'E', 'I', 'L', 'P', 'A', 'X' };

    // As if you are looking at the chip.
    static constexpr std::array<std::array<int32_t, NOC_SIZE_X>, NOC_SIZE_Y> physical_layout = {{
    //                                  1  1  1  1  1  1  1
    //    0  1  2  3  4  5  6  7  8  9  0  1  2  3  4  5  6
        { D, T, T, T, T, T, T, T, T, T, T, T, T, T, T, I, D }, // 11
        { D, T, T, T, T, T, T, T, T, T, T, T, T, T, T, L, D }, // 10
        { D, T, T, T, T, T, T, T, T, T, T, T, T, T, T, L, D }, // 9
        { D, T, T, T, T, T, T, T, T, T, T, T, T, T, T, I, D }, // 8
        { D, T, T, T, T, T, T, T, T, T, T, T, T, T, T, I, D }, // 7
        { D, T, T, T, T, T, T, T, T, T, T, T, T, T, T, L, D }, // 6
        { D, T, T, T, T, T, T, T, T, T, T, T, T, T, T, L, D }, // 5
        { D, T, T, T, T, T, T, T, T, T, T, T, T, T, T, I, D }, // 4
        { D, T, T, T, T, T, T, T, T, T, T, T, T, T, T, I, D }, // 3
        { D, T, T, T, T, T, T, T, T, T, T, T, T, T, T, I, D }, // 2
        { D, E, E, E, E, E, E, E, E, E, E, E, E, E, E, I, D }, // 1
        { D, I, I, P, I, I, I, I, I, I, I, I, P, I, I, A, D }, // 0
    }};

    // Untraslated NOC0 layout.
    static constexpr std::array<std::array<int32_t, NOC_SIZE_X>, NOC_SIZE_Y> noc0_layout = {{
    //                                  1  1  1  1  1  1  1
    //    0  1  2  3  4  5  6  7  8  9  0  1  2  3  4  5  6
        { D, I, P, I, I, I, I, I, A, D, I, P, I, I, I, I, I },  // 0
        { D, E, E, E, E, E, E, E, I, D, E, E, E, E, E, E, E },  // 1
        { D, T, T, T, T, T, T, T, I, D, T, T, T, T, T, T, T },  // 2
        { D, T, T, T, T, T, T, T, L, D, T, T, T, T, T, T, T },  // 3
        { D, T, T, T, T, T, T, T, I, D, T, T, T, T, T, T, T },  // 4
        { D, T, T, T, T, T, T, T, L, D, T, T, T, T, T, T, T },  // 5
        { D, T, T, T, T, T, T, T, I, D, T, T, T, T, T, T, T },  // 6
        { D, T, T, T, T, T, T, T, I, D, T, T, T, T, T, T, T },  // 7
        { D, T, T, T, T, T, T, T, L, D, T, T, T, T, T, T, T },  // 8
        { D, T, T, T, T, T, T, T, I, D, T, T, T, T, T, T, T },  // 9
        { D, T, T, T, T, T, T, T, I, D, T, T, T, T, T, T, T },  // 10
        { D, T, T, T, T, T, T, T, I, D, T, T, T, T, T, T, T },  // 11
    }};
#undef D
#undef T
#undef E
#undef I
#undef L
#undef P
#undef A

    static constexpr std::array<uint32_t, NOC_SIZE_X> PHYS_X_2_NOC0 = { 0, 1, 16, 2, 15, 3, 14, 4, 13, 5, 12, 6, 11, 7, 10, 8, 9 };
    static constexpr std::array<uint32_t, NOC_SIZE_Y> PHYS_Y_2_NOC0 = { 0, 1, 11, 2, 10, 3, 9, 4, 8, 5, 7, 6 };

    static constexpr coord_t noc_to_phys(coord_t noc)
    {
        auto x_it = std::find(PHYS_X_2_NOC0.begin(), PHYS_X_2_NOC0.end(), noc.x);
        auto y_it = std::find(PHYS_Y_2_NOC0.begin(), PHYS_Y_2_NOC0.end(), noc.y);
        return { static_cast<uint32_t>(x_it - PHYS_X_2_NOC0.begin()), static_cast<uint32_t>(y_it - PHYS_Y_2_NOC0.begin()) };
    }

    static constexpr std::array<std::array<int32_t, NOC_SIZE_X>, NOC_SIZE_Y> flip(const auto& g) {
        std::array<std::array<int32_t, NOC_SIZE_X>, NOC_SIZE_Y> flipped = {};
        for (size_t y = 0; y < NOC_SIZE_Y; ++y) {
            flipped[y] = g[NOC_SIZE_Y - 1 - y];
        }
        return flipped;
    }
};

void blackhole_gen_coordinates()
{
    std::vector<std::vector<int>> my_grid(Blackhole::NOC_SIZE_Y, std::vector<int>(Blackhole::NOC_SIZE_X));
    for (uint32_t y = 0; y < Blackhole::NOC_SIZE_Y; y++) {
        for (uint32_t x = 0; x < Blackhole::NOC_SIZE_X; x++) {
            const auto& grid = Blackhole::physical_layout;
            const auto flip = Blackhole::flip(grid);
            auto phys = Blackhole::noc_to_phys({x, y});
            auto node = flip[phys.y][phys.x];
            my_grid[y][x] = node;
        }
    }

    printf("  0  1  2  3  4  5  6  7  8  9  0  1  2  3  4  5  6\n");
    // for (uint32_t y = 0; y < Blackhole::NOC_SIZE_Y; y++) {
    for (int32_t y = Blackhole::NOC_SIZE_Y - 1; y >= 0; --y) {
        printf("%d ", y % 10);
        for (uint32_t x = 0; x < Blackhole::NOC_SIZE_X; x++) {
            auto node = my_grid[y][x];
            char c = Blackhole::C[node];
            printf("%c, ", c);
        }
        printf("\n");
    }
    printf("  0  1  2  3  4  5  6  7  8  9  0  1  2  3  4  5  6\n");
}


int main()
{
    for (auto device_path : Device::enumerate_devices()) {
        Device device(device_path);
        if (!device.is_blackhole()) {
            continue;
        }

        auto tlb = device.map_tlb(8, 0, 0x80030434, CacheMode::Uncached, 1 << 24, 0);

        blackhole_noc_sanity_check(device);
    }
    return 0;
}
