#include "holething.hpp"

#include <map>
#include <random>

using namespace tt;

void blackhole_noc_sanity_check(Device& device)
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
    printf("Blackhole NOC sanity test 1/2 PASSED\n");
}

void blackhole_noc_sanity_check_tlb(Device& device)
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

            TlbWindow tlb(device, 1ULL << 21, TT_MMIO_CACHE_MODE_UC);
            uint32_t node_id = TlbWindowUtils::noc_read32(tlb, x, y, NOC_NODE_ID_LOGICAL);

            // uint32_t node_id = device.noc_read32(x, y, NOC_NODE_ID_LOGICAL);
            uint32_t node_id_x = (node_id >> 0x0) & 0x3f;
            uint32_t node_id_y = (node_id >> 0x6) & 0x3f;

            if (node_id_x != x || node_id_y != y) {
                printf("Node ID mismatch, expected (%u, %u), got (%u, %u)\n", x, y, node_id_x, node_id_y);
                exit(1);
            }
        }
    }
    printf("Blackhole NOC sanity test 2/2 PASSED\n");
}

void wormhole_noc_sanity_check(Device& device)
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

void wormhole_noc_sanity_check_tlb(Device& device)
{
    if (!device.is_wormhole())
        return;

    TlbWindow tlb(device, 1ULL << 21, TT_MMIO_CACHE_MODE_UC);

    {
        constexpr uint32_t ARC_X = 0;
        constexpr uint32_t ARC_Y = 10;
        constexpr uint64_t ARC_NOC_NODE_ID = 0xFFFB2002CULL;

        auto node_id = TlbWindowUtils::noc_read32(tlb, ARC_X, ARC_Y, ARC_NOC_NODE_ID);
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
        TlbWindow tlb(device, 1ULL << 21, TT_MMIO_CACHE_MODE_UC);

        auto node_id = TlbWindowUtils::noc_read32(tlb, DDR_X, DDR_Y, DDR_NOC_NODE_ID);
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
            auto node_id = TlbWindowUtils::noc_read32(tlb, x, y, TENSIX_NOC_NODE_ID);
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
        uint8_t* random_bytes_for_remainder = reinterpret_cast<uint8_t*>(&last_random_chunk);
        for (size_t i = 0; i < rem_bytes; ++i) {
            ptr8[i] = random_bytes_for_remainder[i];
        }
    }
}

void test_noc_dma(Device& device, size_t magnitude)
{
    size_t buffer_size = 1ULL << magnitude;
    DmaBuffer buffer(device, buffer_size);
    uint8_t* data = (uint8_t*)buffer.get_mem();
    uint64_t noc_addr = buffer.get_noc_addr();

    std::vector<uint8_t> pattern(buffer_size);
    fill_with_random_data(pattern.data(), buffer_size);

    auto [x, y] = device.get_pcie_coordinates();
    device.noc_write(x, y, noc_addr, pattern.data(), buffer_size);

    if (memcmp(data, pattern.data(), buffer_size) != 0) {
        printf("Data mismatch\n");
        return;
    }

    printf("NOC DMA test PASSED (size=0x%lx)\n", buffer_size);
}

void test_noc_dma_tlb(Device& device, size_t magnitude)
{
    size_t buffer_size = 1ULL << magnitude;
    DmaBuffer buffer(device, buffer_size);
    uint8_t* data = (uint8_t*)buffer.get_mem();
    uint64_t noc_addr = buffer.get_noc_addr();

    std::vector<uint8_t> pattern(buffer_size);
    fill_with_random_data(pattern.data(), buffer_size);

    auto [x, y] = device.get_pcie_coordinates();
    TlbWindow tlb(device, 1ULL << 21, TT_MMIO_CACHE_MODE_WC);
    TlbWindowUtils::noc_write(tlb, x, y, noc_addr, pattern.data(), buffer_size);

    if (memcmp(data, pattern.data(), buffer_size) != 0) {
        printf("Data mismatch\n");
        return;
    }

    printf("NOC DMA test PASSED (size=0x%lx)\n", buffer_size);
}

void test_telemetry(Device& device)
{
    std::map<const char*, uint32_t> telemetry_tags = {
        {"AI Clock (MHz)", 14},
        {"ASIC temp (C) ", 11},
    };
    for (auto [name, tag] : telemetry_tags) {
        uint32_t telemetry = device.read_telemetry(tag);
        double converted = telemetry;
        if (tag == 11) {
            uint32_t int_part = telemetry >> 16;
            uint32_t frac_part = telemetry & 0xFFFF;
            telemetry = (int_part * 1000) + ((frac_part * 1000) / 0x10000);
            converted = telemetry / 1000.0;
        }
        printf("telemetry: %s = %f\n", name, converted);
    }
}

#define WH_DDR_X 0
#define WH_DDR_Y 0
#define BH_DDR_X 17
#define BH_DDR_Y 12

static uint32_t seed;
static void my_srand(uint32_t new_seed)
{
    seed = new_seed;
}

static inline uint32_t my_rand(void)
{
    seed = seed * 1103515245 + 12345;
    return (uint32_t)(seed / 65536) % 32768;
}

void block_io_test(Device& dev)
{
    uint16_t ddr_x = dev.is_wormhole() ? WH_DDR_X : dev.is_blackhole() ? BH_DDR_X : -1;
    uint16_t ddr_y = dev.is_wormhole() ? WH_DDR_Y : dev.is_blackhole() ? BH_DDR_Y : -1;

    /* Allocate buffer. */
    void* data;
    size_t len = 0x380000; /* 3.5 MiB */
    data = malloc(len);
    if (!data) {
        printf("Failed to allocate memory for data: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    /* Fill buffer with pseudorandom numbers. */
    my_srand(42);
    for (size_t i = 0; i < len / sizeof(uint32_t); i++) {
        ((uint32_t*)data)[i] = my_rand();
    }

    /* Write the buffer and read it back in a few different places */
    uint64_t addresses[] = { 0x000000, 0xF00008, 0x50000C };
    for (size_t i = 0; i < sizeof(addresses) / sizeof(addresses[0]); i++) {
        uint64_t addr = addresses[i];

        /* Write data to the NOC. */
        TlbWindow tlb(dev, 1ULL << 21, TT_MMIO_CACHE_MODE_WC);
        TlbWindowUtils::noc_write(tlb, ddr_x, ddr_y, addr, data, len);

        /* Read it back into a new buffer. */
        void* read_data = malloc(len);
        if (!read_data) {
            printf("Failed to allocate memory for read_data: %s\n", strerror(errno));
            free(data);
            exit(EXIT_FAILURE);
        }
        TlbWindowUtils::noc_read(tlb, ddr_x, ddr_y, addr, read_data, len);

        /* Verify that the data matches. */
        if (memcmp(data, read_data, len) != 0) {
            printf("Data mismatch at address 0x%lx\n", addr);
            free(read_data);
            free(data);
            exit(EXIT_FAILURE);
        }

        free(read_data);
    }

    printf("Block I/O test PASSED\n");
}


void run_tests(Device& device)
{
    blackhole_noc_sanity_check(device);
    blackhole_noc_sanity_check_tlb(device);
    wormhole_noc_sanity_check(device);
    wormhole_noc_sanity_check_tlb(device);
    block_io_test(device);
    test_telemetry(device);
    test_noc_dma(device, 21);
    test_noc_dma(device, 28);
    test_noc_dma(device, 30);
    test_noc_dma_tlb(device, 21);
    test_noc_dma_tlb(device, 28);
    test_noc_dma_tlb(device, 30);
}

int main()
{
    for (auto device_path : DeviceUtils::enumerate_devices()) {
        Device device(device_path.c_str());
        DeviceUtils::print_device_info(device);
        run_tests(device);
    }

    return 0;
}
