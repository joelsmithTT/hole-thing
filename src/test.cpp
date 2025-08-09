#include "holething.hpp"

#include <map>
#include <random>

using namespace tt;

int blackhole_noc_sanity_check(Device& device)
{
    static constexpr uint64_t NOC_NODE_ID_LOGICAL = 0xffb20148ULL;

    if (!device.is_blackhole())
        return -1;

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
                return -1;
            }
        }
    }
    printf("Blackhole NOC sanity test 1/2 PASSED\n");
    return 0;
}

int wormhole_noc_sanity_check(Device& device)
{
    if (!device.is_wormhole())
        return -1;

    {
        constexpr uint32_t ARC_X = 0;
        constexpr uint32_t ARC_Y = 10;
        constexpr uint64_t ARC_NOC_NODE_ID = 0xFFFB2002CULL;

        auto node_id = device.noc_read32(ARC_X, ARC_Y, ARC_NOC_NODE_ID);
        auto x = (node_id >> 0x0) & 0x3f;
        auto y = (node_id >> 0x6) & 0x3f;
        if (x != ARC_X || y != ARC_Y) {
            printf("ARC node ID mismatch, expected (%u, %u), got (%u, %u)\n", ARC_X, ARC_Y, x, y);
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
            printf("DDR node ID mismatch, expected (%u, %u), got (%u, %u)\n", DDR_X, DDR_Y, x, y);
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
                printf("Expected x: %u, y: %u, got x: %u, y: %u\n", x, y, node_id_x, node_id_y);
                return -1;
            }
        }
    }

    printf("Wormhole NOC sanity test PASSED\n");
    return 0;
}

int noc_sanity_check(Device& device)
{
    if (device.is_wormhole()) {
        return wormhole_noc_sanity_check(device);
    } else if (device.is_blackhole()) {
        return blackhole_noc_sanity_check(device);
    } else {
        printf("Unknown device type for NOC sanity check\n");
        return -1;
    }
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

int test_noc_dma(Device& device, DmaBuffer& buffer)
{
    uint8_t* data = (uint8_t*)buffer.get_mem();
    uint64_t noc_addr = buffer.get_noc_addr();
    size_t len = buffer.get_len();

    std::vector<uint8_t> pattern(len);
    fill_with_random_data(pattern.data(), len);

    auto [x, y] = device.get_pcie_coordinates();
    device.noc_write(x, y, noc_addr, pattern.data(), len);
    device.noc_read32(x, y, noc_addr);
    device.noc_read32(x, y, noc_addr + len - sizeof(uint32_t));

    if (memcmp(data, pattern.data(), len) != 0) {
        printf("NOC DMA test FAILED (size=0x%lx)\n", len);
        return -1;
    }

    printf("NOC DMA test PASSED (size=0x%lx)\n", len);
    return 0;
}

int test_noc_dma(Device& device, size_t magnitude)
{
    size_t buffer_size = 1ULL << magnitude;
    DmaBuffer* buffer;
    try {
        buffer = new DmaBuffer(device, buffer_size);
    } catch (const std::system_error& e) {
        printf("NOC DMA test SKIPPED (size=0x%lx): %s\n", buffer_size, e.what());
        // Tips:
        // echo 1 | sudo tee /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages
        // echo 1 | sudo tee /sys/kernel/mm/hugepages/hugepages-1048576kB/nr_hugepages
        // remove `iommu=pt` or any e.g. `intel_iommu=off` from kernel command line and reboot
        return 0; // Skipped but not failed
    }

    int r = test_noc_dma(device, *buffer);
    delete buffer;
    return r;
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


int run_tests(Device& device)
{
    // Can we access NOC registers correctly?
    if (noc_sanity_check(device) != 0) {
        printf("NOC sanity check FAILED\n");
        return -1;
    }

    // 4096 KiB DMA test
    if (test_noc_dma(device, 12) != 0) {
        return -1;
    }
    return 0;

#if 0 // Skip these for now.
    // Skipped becase DDR takes too long to train on 6U WH, so we might not be able to touch it yet.
    block_io_test(device);              // Writes to DDR and reads back

    // Skipped because DDR takes too long to train, FW might not be ready with telemetry yet.
    test_telemetry(device);             // Reads telemetry table

    // ... because no IOMMU, no 2 MiB pages = fail
    test_noc_dma(device, 21);           // 2 MiB DMA test

    // ... because it takes too long
    test_noc_dma(device, 30);           // 1 GiB DMA test
#endif
}

int run(std::string device_path)
{
    try {
        Device device(device_path.c_str());
        DeviceUtils::print_device_info(device);
        return run_tests(device);
    } catch (const std::runtime_error& e) {
        fprintf(stderr, "Error accessing device %s: %s\n", device_path.c_str(), e.what());
        return 1; // Indicate failure
    }
    return 0;
}

void print_usage(const char* prog_name) {
    fprintf(stderr, "Usage: %s <device_id | -1>\n", prog_name);
    fprintf(stderr, "  <device_id>: The ID of the specific device to test (e.g., 0).\n");
    fprintf(stderr, "  -1:          Test all available devices.\n");
}

int main(int argc, char *argv[])
{
    if (argc != 2) {
        print_usage(argv[0]);
        return 1;
    }

    std::string arg = argv[1];

    if (arg == "-1") {
        // Run on all detected devices.
        auto device_paths = DeviceUtils::enumerate_devices();
        if (device_paths.empty()) {
            fprintf(stderr, "No Tenstorrent devices found in /dev/tenstorrent/\n");
            return 1;
        }

        int failures = 0;
        printf("Running %s on all %zu devices...\n\n", argv[0], device_paths.size());
        for (const auto& path : device_paths) {
            if (run(path) != 0) {
                failures++;
            }
            printf("\n"); // Add a newline for better readability between devices
        }

        if (failures > 0) {
            fprintf(stderr, "Finished. %d of %zu devices failed or were skipped.\n", failures, device_paths.size());
            return 1;
        } else {
            printf("Finished. All devices were processed successfully.\n");
            return 0;
        }

    } else {
        // Run on a single, specified device.
        std::string device_path = "/dev/tenstorrent/" + arg;
        return run(device_path);
    }
}
