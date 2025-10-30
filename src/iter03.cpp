#include "holething.hpp"

#include <iostream>
#include <iomanip>
#include <fstream>
#include <vector>
#include <unistd.h>

using namespace tt;

// Telemetry tag
#define TAG_ENABLED_TENSIX_COL 34

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

int popcount(uint32_t x)
{
    int count = 0;
    while (x) {
        count += x & 1;
        x >>= 1;
    }
    return count;
}

bool probe_tensix(Device& device, uint8_t x, uint8_t y, const std::vector<uint8_t>& program, noc_info_t& info)
{
    // Reset Tensix
    device.noc_write32(x, y, TENSIX_RESET_REG, TENSIX_IN_RESET);
    
    // Load program
    device.noc_write(x, y, 0x0, program.data(), program.size());
    
    // Clear ready flag
    device.noc_write32(x, y, READY_ADDR, 0);
    
    // Start Tensix
    device.noc_write32(x, y, TENSIX_RESET_REG, TENSIX_OUT_RESET);
    
    // Poll for ready flag (with timeout)
    uint32_t ready = 0;
    for (int polls = 0; polls < 10; polls++) {
        usleep(10000);  // 10ms
        ready = device.noc_read32(x, y, READY_ADDR);
        if (ready == 0xC0DEC0DE) {
            break;
        }
    }
    
    if (ready != 0xC0DEC0DE) {
        return false;  // Tensix didn't respond
    }
    
    // Read the NOC info
    device.noc_read(x, y, DATA_BASE, &info, sizeof(info));
    
    // Reset again
    device.noc_write32(x, y, TENSIX_RESET_REG, TENSIX_IN_RESET);
    
    return true;
}

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    Device device("/dev/tenstorrent/0");
    DeviceUtils::print_device_info(device);

    // Read telemetry to determine enabled Tensix columns
    uint32_t enabled_cols = device.read_telemetry(TAG_ENABLED_TENSIX_COL);
    int num_cols = popcount(enabled_cols);
    
    std::cout << "\n=== Iteration 03: Scan All Tensix Cores ===\n";
    std::cout << "TAG_ENABLED_TENSIX_COL: 0x" << std::hex << enabled_cols << std::dec 
              << " (" << num_cols << " columns enabled)\n";
    
    if (num_cols == 14) {
        std::cout << "Product: p150 (all Tensix columns present)\n";
    } else if (num_cols == 12) {
        std::cout << "Product: p100 (columns 15-16 harvested)\n";
    } else {
        std::cout << "Product: Unknown configuration\n";
    }
    
    // Load the probe program once
    std::vector<uint8_t> program = read_bin("tensix/iter02.bin");
    std::cout << "Program size: " << program.size() << " bytes\n\n";

    // Determine X range based on column count
    std::vector<uint8_t> x_coords;
    for (uint8_t x = 1; x <= 7; x++) {
        x_coords.push_back(x);
    }
    uint8_t max_right_x = (num_cols == 14) ? 16 : 14;
    for (uint8_t x = 10; x <= max_right_x; x++) {
        x_coords.push_back(x);
    }
    
    std::cout << "Scanning Tensix grid...\n";
    std::cout << "X range: ";
    for (auto x : x_coords) {
        std::cout << (int)x << " ";
    }
    std::cout << "\nY range: 2-11\n\n";
    
    int total = 0;
    int success = 0;
    
    // Scan all Tensix coordinates
    for (uint8_t y = 2; y <= 11; y++) {
        for (auto x : x_coords) {
            total++;
            noc_info_t info;
            
            if (probe_tensix(device, x, y, program, info)) {
                success++;
                
                // Extract key info
                uint32_t noc0_x = (info.noc0_node_id >> 0) & 0x3F;
                uint32_t noc0_y = (info.noc0_node_id >> 6) & 0x3F;
                uint32_t noc1_x = (info.noc1_node_id >> 0) & 0x3F;
                uint32_t noc1_y = (info.noc1_node_id >> 6) & 0x3F;
                uint32_t logical_x = (info.noc0_id_logical >> 0) & 0x3F;
                uint32_t logical_y = (info.noc0_id_logical >> 6) & 0x3F;
                uint32_t tile_idx = (info.noc0_endpoint_id >> 0) & 0xFF;
                
                std::cout << "Tensix[" << std::setw(2) << (int)x << "," << std::setw(2) << (int)y << "] "
                          << "Logical(" << std::setw(2) << logical_x << "," << std::setw(2) << logical_y << ") "
                          << "NoC0(" << std::setw(2) << noc0_x << "," << std::setw(2) << noc0_y << ") "
                          << "NoC1(" << std::setw(2) << noc1_x << "," << std::setw(2) << noc1_y << ") "
                          << "TileIdx=" << std::setw(3) << tile_idx << "\n";
            } else {
                std::cout << "Tensix[" << std::setw(2) << (int)x << "," << std::setw(2) << (int)y << "] TIMEOUT\n";
            }
        }
    }
    
    std::cout << "\nSummary: " << success << "/" << total << " Tensix cores responded\n";
    
    if (num_cols == 14 && success == 140) {
        std::cout << "SUCCESS: All 140 Tensix cores present and responsive (p150)\n";
    } else if (num_cols == 12 && success == 120) {
        std::cout << "SUCCESS: All 120 Tensix cores present and responsive (p100)\n";
    } else {
        std::cout << "WARNING: Unexpected number of responsive cores\n";
    }
    
    std::cout << "\nDone.\n";
    return 0;
}

