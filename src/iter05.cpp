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

// Memory layout (must match tensix/iter05.c)
static constexpr uint64_t SRC_BUF_ADDR_LO  = 0x1000;
static constexpr uint64_t SRC_BUF_ADDR_MID = 0x1004;
static constexpr uint64_t SRC_BUF_ADDR_HI  = 0x1008;
static constexpr uint64_t DST_BUF_ADDR_LO  = 0x100C;
static constexpr uint64_t DST_BUF_ADDR_MID = 0x1010;
static constexpr uint64_t DST_BUF_ADDR_HI  = 0x1014;
static constexpr uint64_t TRANSFER_SIZE    = 0x1018;
static constexpr uint64_t READY_ADDR       = 0x101C;
static constexpr uint64_t DEBUG_SRC_LO      = 0x1020;
static constexpr uint64_t DEBUG_SRC_MID     = 0x1024;
static constexpr uint64_t DEBUG_DST_LO      = 0x1028;
static constexpr uint64_t DEBUG_DST_MID     = 0x102C;
static constexpr uint64_t DEBUG_NODE_ID     = 0x1030;
static constexpr uint64_t DEBUG_LOCAL_COORD = 0x1034;

// GDDR target
static constexpr uint8_t GDDR_X = 17;
static constexpr uint8_t GDDR_Y = 12;

// Buffer size
static constexpr size_t BUFFER_SIZE = 4ULL * 1024 * 1024;

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

    std::cout << "\n=== Iteration 05: Four-Hop DMA via GDDR ===\n";
    std::cout << "Path: PCIe -> L1 -> GDDR -> L1 -> PCIe\n";
    std::cout << "Buffer size: " << (BUFFER_SIZE / (1024*1024)) << " MB\n\n";

    // Allocate two DMA buffers
    std::cout << "1. Allocating source buffer...\n";
    DmaBuffer src_buffer(device, BUFFER_SIZE);
    uint64_t src_noc_addr = src_buffer.get_noc_addr();
    uint64_t src_iova = src_buffer.get_iova();
    std::cout << "   NOC address: 0x" << std::hex << src_noc_addr << std::dec << "\n";
    std::cout << "   IOVA: 0x" << std::hex << src_iova << std::dec << "\n";

    std::cout << "2. Allocating destination buffer...\n";
    DmaBuffer dst_buffer(device, BUFFER_SIZE);
    uint64_t dst_noc_addr = dst_buffer.get_noc_addr();
    uint64_t dst_iova = dst_buffer.get_iova();
    std::cout << "   NOC address: 0x" << std::hex << dst_noc_addr << std::dec << "\n";
    std::cout << "   IOVA: 0x" << std::hex << dst_iova << std::dec << "\n";

    auto [pcie_x, pcie_y] = device.get_pcie_coordinates();

    // Fill source with pattern, clear destination
    std::cout << "3. Filling buffers...\n";
    uint32_t* src_ptr = (uint32_t*)src_buffer.get_mem();
    uint32_t* dst_ptr = (uint32_t*)dst_buffer.get_mem();
    size_t num_words = BUFFER_SIZE / sizeof(uint32_t);

    for (size_t i = 0; i < num_words; i++) {
        src_ptr[i] = (uint32_t)i;
        dst_ptr[i] = 0;
    }

    // Test: Try Tensix reading a single word to see what happens
    std::cout << "3a. Debug: write test pattern and let Tensix read it...\n";
    src_ptr[0] = 0xDEADBEEF;
    src_ptr[1] = 0xCAFEBABE;
    std::cout << "   Set src[0]=0xDEADBEEF, src[1]=0xCAFEBABE\n";

    // Load program
    std::vector<uint8_t> program = read_bin("tensix/iter05.bin");
    std::cout << "4. Loading Tensix program (" << program.size() << " bytes)...\n";

    device.noc_write32(TENSIX_X, TENSIX_Y, TENSIX_RESET_REG, TENSIX_IN_RESET);
    device.noc_write(TENSIX_X, TENSIX_Y, 0x0, program.data(), program.size());

    // Write parameters
    std::cout << "5. Writing parameters...\n";
    uint32_t pcie_coord = (pcie_y << 6) | pcie_x;

    std::cout << "   Sending src to Tensix: lo=0x" << std::hex << (uint32_t)(src_noc_addr & 0xFFFFFFFF)
              << " mid=0x" << (uint32_t)(src_noc_addr >> 32) << " hi=0x" << pcie_coord << std::dec << "\n";
    std::cout << "   Sending dst to Tensix: lo=0x" << std::hex << (uint32_t)(dst_noc_addr & 0xFFFFFFFF)
              << " mid=0x" << (uint32_t)(dst_noc_addr >> 32) << " hi=0x" << pcie_coord << std::dec << "\n";

    device.noc_write32(TENSIX_X, TENSIX_Y, SRC_BUF_ADDR_LO, (uint32_t)(src_noc_addr & 0xFFFFFFFF));
    device.noc_write32(TENSIX_X, TENSIX_Y, SRC_BUF_ADDR_MID, (uint32_t)(src_noc_addr >> 32));
    device.noc_write32(TENSIX_X, TENSIX_Y, SRC_BUF_ADDR_HI, pcie_coord);

    device.noc_write32(TENSIX_X, TENSIX_Y, DST_BUF_ADDR_LO, (uint32_t)(dst_noc_addr & 0xFFFFFFFF));
    device.noc_write32(TENSIX_X, TENSIX_Y, DST_BUF_ADDR_MID, (uint32_t)(dst_noc_addr >> 32));
    device.noc_write32(TENSIX_X, TENSIX_Y, DST_BUF_ADDR_HI, pcie_coord);

    device.noc_write32(TENSIX_X, TENSIX_Y, TRANSFER_SIZE, (uint32_t)BUFFER_SIZE);
    device.noc_write32(TENSIX_X, TENSIX_Y, READY_ADDR, 0);

    // Start
    std::cout << "6. Starting Tensix...\n";

    uint32_t noc_cfg = device.noc_read32(TENSIX_X, TENSIX_Y, 0xFFB20100); // NIU_CFG_0
    uint32_t noc_id_logical = device.noc_read32(TENSIX_X, TENSIX_Y, 0xFFB20148);
    printf("NIU_CFG_0: 0x%x (coord translation bit 14: %d)\n", noc_cfg, (noc_cfg >> 14) & 1);
    printf("NOC_ID_LOGICAL: 0x%x\n", noc_id_logical);

    device.noc_write32(TENSIX_X, TENSIX_Y, TENSIX_RESET_REG, TENSIX_OUT_RESET);

    // Poll with timeout
    std::cout << "7. Waiting for completion...\n";
    uint32_t ready = 0;
    int polls = 0;
    auto start = std::chrono::steady_clock::now();

    while (ready != 0xC0DEC0DE && polls < 100) {  // 10s timeout
        usleep(100000);  // 100ms
        ready = device.noc_read32(TENSIX_X, TENSIX_Y, READY_ADDR);
        polls++;

        if (polls % 10 == 0 || (ready != 0 && ready != 0xAAAAAAAA)) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();

            std::cout << "   Poll " << polls << " (" << (elapsed/1000.0) << "s) "
                      << "Ready=0x" << std::hex << ready << std::dec;

            if (ready == 0x11111111) {
                std::cout << " [Phase 1: PCIe->L1->GDDR]";
            } else if (ready == 0x22222222) {
                std::cout << " [Phase 2: GDDR->L1->PCIe]";
            }
            std::cout << "\n";

            if (ready == 0xC0DEC0DE) break;
        }
    }

    auto end = std::chrono::steady_clock::now();
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    if (ready != 0xC0DEC0DE) {
        std::cout << "ERROR: Timeout (ready = 0x" << std::hex << ready << std::dec << ")\n";
        return 1;
    }

    std::cout << "   Completed in " << (elapsed_ms / 1000.0) << " seconds\n";

    // Verify
    std::cout << "8. Verifying data...\n";
    std::cout << "   src[0]=" << src_ptr[0] << " src[1]=" << src_ptr[1] << "\n";
    std::cout << "   dst[0]=" << dst_ptr[0] << " dst[1]=" << dst_ptr[1] << "\n";

    // Check GDDR contents after transfer
    uint32_t gddr_0 = device.noc_read32(GDDR_X, GDDR_Y, 0x0);
    uint32_t gddr_4 = device.noc_read32(GDDR_X, GDDR_Y, 0x4);
    std::cout << "   GDDR[0]=" << gddr_0 << " GDDR[1]=" << gddr_4 << "\n";

    // Check what Tensix saw
    uint32_t total_size_tensix = device.noc_read32(TENSIX_X, TENSIX_Y, DEBUG_SRC_LO);
    uint32_t chunks_phase1 = device.noc_read32(TENSIX_X, TENSIX_Y, DEBUG_SRC_MID);
    uint32_t chunks_phase2 = device.noc_read32(TENSIX_X, TENSIX_Y, DEBUG_DST_MID);
    uint32_t node_id_tensix = device.noc_read32(TENSIX_X, TENSIX_Y, DEBUG_NODE_ID);
    uint32_t local_coord_tensix = device.noc_read32(TENSIX_X, TENSIX_Y, DEBUG_LOCAL_COORD);

    std::cout << "   Tensix saw transfer_size: " << total_size_tensix << " bytes\n";
    std::cout << "   Tensix node_id: 0x" << std::hex << node_id_tensix << std::dec 
              << " (" << (node_id_tensix & 0x3F) << ", " << ((node_id_tensix >> 6) & 0x3F) << ")\n";
    std::cout << "   Tensix local_coord: 0x" << std::hex << local_coord_tensix << std::dec << "\n";
    std::cout << "   Tensix did phase1 chunks: " << chunks_phase1 << "\n";
    std::cout << "   Tensix did phase2 chunks: " << chunks_phase2 << "\n";

    uint32_t tensix_dst_lo = device.noc_read32(TENSIX_X, TENSIX_Y, DEBUG_DST_LO);

    uint64_t tensix_saw_src = src_noc_addr;
    uint64_t tensix_saw_dst = dst_noc_addr;

    std::cout << "   Host sent  src: 0x" << std::hex << src_noc_addr << std::dec << "\n";
    std::cout << "   Tensix saw src: 0x" << std::hex << tensix_saw_src << std::dec << "\n";
    std::cout << "   Host sent  dst: 0x" << std::hex << dst_noc_addr << std::dec << "\n";
    std::cout << "   Tensix saw dst: 0x" << std::hex << tensix_saw_dst << std::dec << "\n";

    if (dst_ptr[0] != 0 && dst_ptr[0] != 0xDEADBEEF) {
        // Offset detected (only if not matching expected pattern)
        uint32_t offset = dst_ptr[0];
        std::cout << "   Offset detected: " << offset << " (0x" << std::hex << offset << std::dec << ")\n";
        std::cout << "   This is " << (offset * 4) << " bytes = " << (offset * 4 / 1024) << " KB\n";
        
        if (offset < num_words) {
            std::cout << "   Checking src[" << offset << "] = " << src_ptr[offset] << "\n";
            // Check if maybe the whole transfer is offset
            if (src_ptr[offset] == offset) {
                std::cout << "   Pattern matches at offset! NOC read started at wrong address\n";
            }
        } else {
            std::cout << "   Offset " << offset << " is out of bounds (num_words=" << num_words << ")\n";
        }
    }

    int errors = 0;
    // Check first two words specially
    if (dst_ptr[0] != 0xDEADBEEF) {
        std::cout << "   ERROR at [0]: expected 0xDEADBEEF (3735928559), got " << dst_ptr[0] << "\n";
        errors++;
    }
    if (dst_ptr[1] != 0xCAFEBABE) {
        std::cout << "   ERROR at [1]: expected 0xCAFEBABE (3405691582), got " << dst_ptr[1] << "\n";
        errors++;
    }

    // Check remaining words
    for (size_t i = 2; i < num_words && errors < 10; i++) {
        if (dst_ptr[i] != (uint32_t)i) {
            std::cout << "   ERROR at [" << i << "]: expected " << (uint32_t)i
                      << ", got " << dst_ptr[i] << "\n";
            errors++;
        }
    }

    if (errors == 0) {
        std::cout << "   SUCCESS! All " << (BUFFER_SIZE / (1024*1024)) << " MB transferred correctly\n";
        std::cout << "   Path: PCIe -> Tensix L1 -> GDDR -> Tensix L1 -> PCIe\n";
    }

    device.noc_write32(TENSIX_X, TENSIX_Y, TENSIX_RESET_REG, TENSIX_IN_RESET);
    std::cout << "\nDone.\n";
    return errors > 0 ? 1 : 0;
}

