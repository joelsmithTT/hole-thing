#include "holething.hpp"

#include <iostream>
#include <iomanip>
#include <fstream>
#include <vector>
#include <unistd.h>

using namespace tt;

// Tensix reset register
static constexpr uint64_t TENSIX_REG_BASE   = 0xFFB00000;
static constexpr uint64_t TENSIX_RESET_REG  = TENSIX_REG_BASE + 0x121B0;
static constexpr uint32_t TENSIX_IN_RESET   = 0x47800;
static constexpr uint32_t TENSIX_OUT_RESET  = 0x47000;

// Memory layout (must match tensix/iter02.c)
static constexpr uint64_t DATA_BASE  = 0x1000;
static constexpr uint64_t READY_ADDR = 0x1100;

struct noc_info_t {
    uint32_t noc0_node_id;
    uint32_t noc0_endpoint_id;
    uint32_t noc0_id_logical;
    uint32_t noc1_node_id;
    uint32_t noc1_endpoint_id;
    uint32_t noc1_id_logical;
};

std::vector<uint8_t> read_bin(const char *filename)
{
    std::ifstream file(filename, std::ios::binary | std::ios::ate);
    if (!file) {
        throw std::runtime_error(std::string("Error opening ") + filename);
    }

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> data(size);
    if (!file.read(reinterpret_cast<char*>(data.data()), size)) {
        throw std::runtime_error(std::string("Error reading from ") + filename);
    }

    return data;
}

void print_node_id(const char* label, uint32_t node_id)
{
    uint32_t x = (node_id >> 0) & 0x3F;
    uint32_t y = (node_id >> 6) & 0x3F;
    uint32_t width = (node_id >> 12) & 0x7F;
    uint32_t height = (node_id >> 19) & 0x7F;
    bool dateline_x = (node_id >> 26) & 1;
    bool dateline_y = (node_id >> 27) & 1;
    bool x_first = (node_id >> 28) & 1;
    
    std::cout << label << ":\n";
    std::cout << "  X=" << x << ", Y=" << y << "\n";
    std::cout << "  NoC size: " << width << "x" << height << "\n";
    std::cout << "  Dateline: X=" << dateline_x << ", Y=" << dateline_y << "\n";
    std::cout << "  Routing: " << (x_first ? "X-first" : "Y-first") << "\n";
}

void print_endpoint_id(const char* label, uint32_t endpoint_id)
{
    uint32_t tile_index = (endpoint_id >> 0) & 0xFF;
    uint32_t tile_type = (endpoint_id >> 8) & 0xFFFF;
    uint32_t noc_index = (endpoint_id >> 24) & 0xFF;
    
    std::cout << label << ":\n";
    std::cout << "  Tile index: " << tile_index << "\n";
    std::cout << "  Tile type: 0x" << std::hex << tile_type << std::dec;
    switch (tile_type) {
        case 0x0100: std::cout << " (Tensix)"; break;
        case 0x0200: std::cout << " (Ethernet)"; break;
        case 0x0300: std::cout << " (PCIe)"; break;
        case 0x0500: std::cout << " (ARC)"; break;
        case 0x0800: std::cout << " (DRAM)"; break;
        case 0x0901: std::cout << " (L2CPU)"; break;
        case 0x0A00: std::cout << " (Security)"; break;
        default: std::cout << " (Unknown)"; break;
    }
    std::cout << "\n";
    std::cout << "  NoC index: " << noc_index << "\n";
}

void print_logical_id(const char* label, uint32_t logical_id)
{
    uint32_t x = (logical_id >> 0) & 0x3F;
    uint32_t y = (logical_id >> 6) & 0x3F;
    
    std::cout << label << ":\n";
    std::cout << "  Translated X=" << x << ", Y=" << y << "\n";
}

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    Device device("/dev/tenstorrent/0");
    DeviceUtils::print_device_info(device);

    constexpr uint8_t TENSIX_X = 2;
    constexpr uint8_t TENSIX_Y = 2;

    // Load program
    std::vector<uint8_t> program = read_bin("tensix/iter02.bin");
    std::cout << "\n=== Iteration 02: NOC Information ===\n";
    std::cout << "Program size: " << program.size() << " bytes\n\n";

    // Reset Tensix
    std::cout << "1. Resetting Tensix...\n";
    device.noc_write32(TENSIX_X, TENSIX_Y, TENSIX_RESET_REG, TENSIX_IN_RESET);

    // Load program
    std::cout << "2. Loading program...\n";
    device.noc_write(TENSIX_X, TENSIX_Y, 0x0, program.data(), program.size());

    // Clear ready flag
    std::cout << "3. Clearing ready flag...\n";
    device.noc_write32(TENSIX_X, TENSIX_Y, READY_ADDR, 0);

    // Start Tensix
    std::cout << "4. Starting Tensix...\n";
    device.noc_write32(TENSIX_X, TENSIX_Y, TENSIX_RESET_REG, TENSIX_OUT_RESET);

    // Poll for ready flag
    std::cout << "5. Waiting for Tensix to complete...\n";
    uint32_t ready = 0;
    int polls = 0;
    while (ready != 0xC0DEC0DE && polls < 100) {
        usleep(10000);  // 10ms
        ready = device.noc_read32(TENSIX_X, TENSIX_Y, READY_ADDR);
        polls++;
    }
    
    if (ready != 0xC0DEC0DE) {
        std::cout << "ERROR: Tensix did not complete (ready = 0x" 
                  << std::hex << ready << std::dec << ")\n";
        return 1;
    }
    std::cout << "   Ready after " << polls << " polls\n\n";

    // Read the NOC info structure
    std::cout << "6. Reading NOC information...\n";
    noc_info_t info;
    device.noc_read(TENSIX_X, TENSIX_Y, DATA_BASE, &info, sizeof(info));

    // Display the information
    std::cout << "\n--- NoC 0 Information ---\n";
    print_node_id("NOC_NODE_ID", info.noc0_node_id);
    print_endpoint_id("NOC_ENDPOINT_ID", info.noc0_endpoint_id);
    print_logical_id("NOC_ID_LOGICAL", info.noc0_id_logical);

    std::cout << "\n--- NoC 1 Information ---\n";
    print_node_id("NOC_NODE_ID", info.noc1_node_id);
    print_endpoint_id("NOC_ENDPOINT_ID", info.noc1_endpoint_id);
    print_logical_id("NOC_ID_LOGICAL", info.noc1_id_logical);

    // Reset
    device.noc_write32(TENSIX_X, TENSIX_Y, TENSIX_RESET_REG, TENSIX_IN_RESET);
    std::cout << "\nDone.\n";

    return 0;
}

