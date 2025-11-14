#include "holething.hpp"

#include <iostream>
#include <iomanip>
#include <fstream>
#include <vector>
#include <unistd.h>
#include <cstring>

using namespace tt;

// Tensix reset register
static constexpr uint64_t TENSIX_REG_BASE   = 0xFFB00000;
static constexpr uint64_t TENSIX_RESET_REG  = TENSIX_REG_BASE + 0x121B0;
static constexpr uint32_t TENSIX_IN_RESET   = 0x47800;
static constexpr uint32_t TENSIX_OUT_RESET  = 0x47000;

// Memory layout (must match tensix/iter04.c)
static constexpr uint64_t HOST_BUF_ADDR_LO  = 0x1000;
static constexpr uint64_t HOST_BUF_ADDR_MID = 0x1004;
static constexpr uint64_t HOST_BUF_ADDR_HI  = 0x1008;
static constexpr uint64_t READY_ADDR        = 0x100C;

// Buffer layout
static constexpr size_t NUM_ELEMENTS = 512;
static constexpr size_t BUFFER_SIZE  = 4096;

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

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    Device device("/dev/tenstorrent/0");
    DeviceUtils::print_device_info(device);

    constexpr uint8_t TENSIX_X = 2;
    constexpr uint8_t TENSIX_Y = 2;

    std::cout << "\n=== Iteration 04: NOC Read/Write with Host Buffer ===\n";

    // Allocate DMA buffer
    std::cout << "1. Allocating host DMA buffer (" << BUFFER_SIZE << " bytes)...\n";
    DmaBuffer buffer(device, BUFFER_SIZE);
    uint64_t noc_addr = buffer.get_noc_addr();

    std::cout << "   NOC address: 0x" << std::hex << noc_addr << std::dec << "\n";

    // Get PCIe coordinates for the NOC address
    auto [pcie_x, pcie_y] = device.get_pcie_coordinates();
    std::cout << "   PCIe tile coordinates: (" << (int)pcie_x << ", " << (int)pcie_y << ")\n";

    // Setup buffer layout
    uint16_t* v0 = (uint16_t*)buffer.get_mem();
    uint16_t* v1 = v0 + NUM_ELEMENTS;
    uint32_t* sum = (uint32_t*)(v1 + NUM_ELEMENTS);

    // Fill test vectors
    std::cout << "2. Filling test vectors...\n";
    for (size_t i = 0; i < NUM_ELEMENTS; i++) {
        v0[i] = i;
        v1[i] = i * 2;
        sum[i] = 0;  // Clear sum area
    }

    // Load Tensix program
    std::vector<uint8_t> program = read_bin("tensix/iter04.bin");
    std::cout << "3. Loading Tensix program (" << program.size() << " bytes)...\n";

    // Reset Tensix
    device.noc_write32(TENSIX_X, TENSIX_Y, TENSIX_RESET_REG, TENSIX_IN_RESET);

    // Load program
    device.noc_write(TENSIX_X, TENSIX_Y, 0x0, program.data(), program.size());

    // Write host buffer NOC address to Tensix L1
    std::cout << "4. Writing buffer address to Tensix L1...\n";
    uint32_t addr_lo = (uint32_t)(noc_addr & 0xFFFFFFFF);
    uint32_t addr_mid = (uint32_t)(noc_addr >> 32);
    uint32_t addr_hi = (pcie_y << 6) | pcie_x;

    device.noc_write32(TENSIX_X, TENSIX_Y, HOST_BUF_ADDR_LO, addr_lo);
    device.noc_write32(TENSIX_X, TENSIX_Y, HOST_BUF_ADDR_MID, addr_mid);
    device.noc_write32(TENSIX_X, TENSIX_Y, HOST_BUF_ADDR_HI, addr_hi);
    device.noc_write32(TENSIX_X, TENSIX_Y, READY_ADDR, 0);

    // Start Tensix
    std::cout << "5. Starting Tensix...\n";
    device.noc_write32(TENSIX_X, TENSIX_Y, TENSIX_RESET_REG, TENSIX_OUT_RESET);

    // Poll for completion
    std::cout << "6. Waiting for Tensix to complete...\n";
    uint32_t ready = 0;
    int polls = 0;
    while (ready != 0xC0DEC0DE && polls < 1000) {
        usleep(10000);  // 10ms
        ready = device.noc_read32(TENSIX_X, TENSIX_Y, READY_ADDR);
        polls++;
    }

    if (ready != 0xC0DEC0DE) {
        std::cout << "ERROR: Tensix did not complete (ready = 0x"
                  << std::hex << ready << std::dec << ")\n";
        return 1;
    }
    std::cout << "   Completed after " << polls << " polls\n";

    // Verify results
    std::cout << "7. Verifying results...\n";

    // Check debug values
    uint32_t debug_v0_0 = device.noc_read32(TENSIX_X, TENSIX_Y, 0x3000);
    uint32_t debug_v0_1 = device.noc_read32(TENSIX_X, TENSIX_Y, 0x3004);
    uint32_t debug_v1_0 = device.noc_read32(TENSIX_X, TENSIX_Y, 0x3008);
    uint32_t debug_v1_1 = device.noc_read32(TENSIX_X, TENSIX_Y, 0x300C);

    std::cout << "   Tensix saw: v0[0]=" << debug_v0_0 << " v0[1]=" << debug_v0_1
              << " v1[0]=" << debug_v1_0 << " v1[1]=" << debug_v1_1 << "\n";
    std::cout << "   Host sent: v0[0]=" << v0[0] << " v0[1]=" << v0[1]
              << " v1[0]=" << v1[0] << " v1[1]=" << v1[1] << "\n";
    std::cout << "   Result: sum[0]=" << sum[0] << " sum[1]=" << sum[1] << "\n";

    int errors = 0;
    for (size_t i = 0; i < NUM_ELEMENTS; i++) {
        uint32_t expected = (uint32_t)v0[i] + (uint32_t)v1[i];
        if (sum[i] != expected) {
            if (errors < 10) {
                std::cout << "   ERROR at [" << i << "]: expected " << expected
                          << ", got " << sum[i] << "\n";
            }
            errors++;
        }
    }

    if (errors == 0) {
        std::cout << "   SUCCESS! All " << NUM_ELEMENTS << " results correct\n";
        std::cout << "   Sample: sum[0] = " << sum[0] << " (v0[0]=" << v0[0]
                  << " + v1[0]=" << v1[0] << ")\n";
        std::cout << "   Sample: sum[" << (NUM_ELEMENTS-1) << "] = " << sum[NUM_ELEMENTS-1]
                  << " (v0[" << (NUM_ELEMENTS-1) << "]=" << v0[NUM_ELEMENTS-1]
                  << " + v1[" << (NUM_ELEMENTS-1) << "]=" << v1[NUM_ELEMENTS-1] << ")\n";
    } else {
        std::cout << "   FAILURE! " << errors << " errors found\n";
    }

    // Reset Tensix
    device.noc_write32(TENSIX_X, TENSIX_Y, TENSIX_RESET_REG, TENSIX_IN_RESET);

    std::cout << "\nDone.\n";
    return errors > 0 ? 1 : 0;
}

