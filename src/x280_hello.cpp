// X280 Hello World - Host-side loader
//
// This loads x280/hello.bin into L3 LIM and boots hart 0.
// Then it polls the mailbox to verify the firmware is running.
//
// Implements the same boot sequence as tt-bh-linux/boot.py

#include "holething.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <thread>
#include <chrono>
#include <vector>

// L2CPU tile coordinates (NOC0) - from tt-bh-linux/console/l2cpu.cpp
// 0: (8,3), 1: (8,9), 2: (8,5), 3: (8,7)
static constexpr uint16_t L2CPU0_X = 8;
static constexpr uint16_t L2CPU0_Y = 3;

// ARC tile for reset and clock control
static constexpr uint16_t ARC_X = 8;
static constexpr uint16_t ARC_Y = 0;

// Addresses within L2CPU tile (NOC address space)
static constexpr uint64_t L3_LIM_BASE = 0x0800'0000ULL;
static constexpr uint64_t L2CPU_EXT_PERIPH_BASE = 0xFFFF'F7FE'FFF1'0000ULL;
static constexpr uint64_t RESET_VECTOR_HART0 = L2CPU_EXT_PERIPH_BASE + 0x00;  // 8 bytes per hart

// L3 cache configuration (within L2CPU)
static constexpr uint64_t L3_CACHE_WAYENABLE = 0x0201'0008ULL;

// L2 prefetcher configuration (within L2CPU)
static constexpr uint64_t L2_PREFETCH_BASE = 0x0203'0000ULL;

// ARC tile addresses
static constexpr uint64_t ARC_L2CPU_RESET = 0x8003'0014ULL;

// PLL4 controls L2CPU clock (in ARC tile address space)
static constexpr uint64_t PLL4_BASE = 0x8002'0500ULL;
static constexpr uint64_t PLL_CNTL_1 = PLL4_BASE + 0x04;
static constexpr uint64_t PLL_CNTL_5 = PLL4_BASE + 0x14;

// Hart status register (in external peripherals)
// 0x0400-0x0401: cease/halt/wfi/debug status (16 bits)
// 0x0402-0x0403: suppress instruction fetch flags (16 bits)  
// Read as 32-bit from 0x0400 to get both
static constexpr uint64_t HART_STATUS_AND_SUPPRESS = L2CPU_EXT_PERIPH_BASE + 0x0400;

// SLPC (all harts wfi/cease status) - internal device  
static constexpr uint64_t SLPC_STATUS = 0x0300'8000ULL;

// Cached DRAM (what tt-bh-linux uses) - 0x4000_3000_0000
// This is the same physical DRAM but with caching enabled from X280's perspective
static constexpr uint64_t DRAM_CACHED_BASE = 0x4000'3000'0000ULL;

// Mailbox structure (must match hello.c)
static constexpr uint64_t MAILBOX_BASE = 0x0800'1000ULL;
static constexpr uint64_t MAGIC_EXPECTED = 0x5846696E694C4548ULL;  // "HeLiFinX"
static constexpr uint32_t MAGIC_MINIMAL = 0xDEADBEEF;  // minimal.S uses this

struct Mailbox {
    uint64_t magic;
    uint64_t heartbeat;
    uint64_t command;
    uint64_t response;
    uint64_t hart_id;
    uint64_t scratch[3];
};

static std::vector<uint8_t> read_binary_file(const char* path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        throw std::runtime_error(std::string("Cannot open file: ") + path);
    }
    
    auto size = file.tellg();
    file.seekg(0);
    
    std::vector<uint8_t> data(size);
    file.read(reinterpret_cast<char*>(data.data()), size);
    
    // Pad to 4-byte alignment
    while (data.size() % 4 != 0) {
        data.push_back(0);
    }
    
    return data;
}

static uint64_t noc_read64(tt::Device& dev, uint16_t x, uint16_t y, uint64_t addr)
{
    uint32_t lo = dev.noc_read32(x, y, addr);
    uint32_t hi = dev.noc_read32(x, y, addr + 4);
    return ((uint64_t)hi << 32) | lo;
}

static void noc_write64(tt::Device& dev, uint16_t x, uint16_t y, uint64_t addr, uint64_t value)
{
    dev.noc_write32(x, y, addr, (uint32_t)(value & 0xFFFFFFFF));
    dev.noc_write32(x, y, addr + 4, (uint32_t)(value >> 32));
}

// Clock configuration - matches tt-bh-linux/clock.py
// PLL4 controls the L2CPU clock
// Must set to low speed (200 MHz) before reset, then raise to high speed (1750 MHz) after

struct PllConfig {
    uint16_t fbdiv;
    uint8_t postdiv[4];
};

static const PllConfig PLL_200MHZ  = { 128, {15, 15, 15, 15} };
static const PllConfig PLL_1750MHZ = { 140, { 1,  1,  1,  1} };

static void set_l2cpu_pll(tt::Device& dev, const PllConfig& target)
{
    // Read current state
    uint32_t cntl1 = dev.noc_read32(ARC_X, ARC_Y, PLL_CNTL_1);
    uint32_t cntl5 = dev.noc_read32(ARC_X, ARC_Y, PLL_CNTL_5);
    
    uint16_t cur_fbdiv = (cntl1 >> 16) & 0xFFFF;
    uint8_t cur_postdiv[4] = {
        (uint8_t)(cntl5 & 0xFF),
        (uint8_t)((cntl5 >> 8) & 0xFF),
        (uint8_t)((cntl5 >> 16) & 0xFF),
        (uint8_t)((cntl5 >> 24) & 0xFF)
    };
    
    // Step postdividers that need to INCREASE first (slows clock)
    for (int i = 0; i < 4; i++) {
        while (cur_postdiv[i] < target.postdiv[i]) {
            cur_postdiv[i]++;
            cntl5 = cur_postdiv[0] | (cur_postdiv[1] << 8) | 
                    (cur_postdiv[2] << 16) | (cur_postdiv[3] << 24);
            dev.noc_write32(ARC_X, ARC_Y, PLL_CNTL_5, cntl5);
            std::this_thread::sleep_for(std::chrono::nanoseconds(1));
        }
    }
    
    // Step fbdiv
    while (cur_fbdiv != target.fbdiv) {
        if (cur_fbdiv < target.fbdiv) cur_fbdiv++;
        else cur_fbdiv--;
        cntl1 = (cntl1 & 0xFFFF) | ((uint32_t)cur_fbdiv << 16);
        dev.noc_write32(ARC_X, ARC_Y, PLL_CNTL_1, cntl1);
        std::this_thread::sleep_for(std::chrono::nanoseconds(1));
    }
    
    // Step postdividers that need to DECREASE (speeds up clock)
    for (int i = 0; i < 4; i++) {
        while (cur_postdiv[i] > target.postdiv[i]) {
            cur_postdiv[i]--;
            cntl5 = cur_postdiv[0] | (cur_postdiv[1] << 8) | 
                    (cur_postdiv[2] << 16) | (cur_postdiv[3] << 24);
            dev.noc_write32(ARC_X, ARC_Y, PLL_CNTL_5, cntl5);
            std::this_thread::sleep_for(std::chrono::nanoseconds(1));
        }
    }
}

static void configure_l2_prefetchers(tt::Device& dev, uint16_t x, uint16_t y)
{
    // Recommended values from SiFive / tt-bh-linux
    const uint32_t basic_control = 0x15811;
    const uint32_t user_control = 0x38c84e;
    
    // One prefetcher per hart
    for (uint64_t offset : {0x0000ULL, 0x2000ULL, 0x4000ULL, 0x6000ULL}) {
        dev.noc_write32(x, y, L2_PREFETCH_BASE + offset, basic_control);
        dev.noc_write32(x, y, L2_PREFETCH_BASE + offset + 4, user_control);
    }
}

int main(int argc, char* argv[])
{
    const char* bin_path = "x280/hello.bin";
    bool use_dram = false;
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--dram") == 0) {
            use_dram = true;
        } else {
            bin_path = argv[i];
        }
    }
    
    // Choose load address
    uint64_t load_addr = use_dram ? DRAM_CACHED_BASE : L3_LIM_BASE;
    uint64_t mailbox_addr = load_addr + 0x1000;  // 4K after code (firmware uses PC-relative)
    
    printf("X280 Hello World Loader\n");
    printf("=======================\n\n");
    printf("Using %s at 0x%llx\n", use_dram ? "DRAM (cached)" : "L3 LIM", 
           (unsigned long long)load_addr);
    
    try {
        // Open device
        tt::Device device("/dev/tenstorrent/0");
        if (!device.is_blackhole()) {
            fprintf(stderr, "Error: This program requires a Blackhole device\n");
            return 1;
        }
        printf("Opened Blackhole device\n");
        
        // Read firmware binary
        printf("Loading firmware from %s\n", bin_path);
        auto firmware = read_binary_file(bin_path);
        printf("  Firmware size: %zu bytes\n", firmware.size());
        
        // Write firmware to chosen location
        printf("Writing firmware to L2CPU0 at 0x%llx...\n", 
               (unsigned long long)load_addr);
        device.noc_write(L2CPU0_X, L2CPU0_Y, load_addr, firmware.data(), firmware.size());
        
        // Verify write by reading back first few words
        uint32_t verify[4];
        for (int i = 0; i < 4; i++) {
            verify[i] = device.noc_read32(L2CPU0_X, L2CPU0_Y, load_addr + i*4);
        }
        printf("  Verify: %08x %08x %08x %08x\n", verify[0], verify[1], verify[2], verify[3]);
        
        // Configure L3 cache BEFORE loading code (like boot.py does)
        // When using DRAM: set wayenable = 0xF (all cache, no LIM)
        // When using LIM: leave at 0 (1 way cache + 15 ways LIM)
        uint32_t wayenable = device.noc_read32(L2CPU0_X, L2CPU0_Y, L3_CACHE_WAYENABLE);
        printf("L3 WAYENABLE before: 0x%x\n", wayenable);
        if (use_dram) {
            printf("  Setting WAYENABLE = 0xF (all cache for DRAM mode)...\n");
            device.noc_write32(L2CPU0_X, L2CPU0_Y, L3_CACHE_WAYENABLE, 0xF);
            wayenable = device.noc_read32(L2CPU0_X, L2CPU0_Y, L3_CACHE_WAYENABLE);
            printf("  WAYENABLE after: 0x%x\n", wayenable);
        }
        
        // Clear mailbox area (firmware uses PC-relative, so mailbox is at load_addr + 0x1000)
        printf("Clearing mailbox at 0x%llx...\n", (unsigned long long)mailbox_addr);
        for (int i = 0; i < 8; i++) {
            noc_write64(device, L2CPU0_X, L2CPU0_Y, mailbox_addr + i*8, 0);
        }
        
        // Set reset vector for ALL harts to point at our code
        // (Even though we only care about hart 0, set them all to be safe)
        printf("Setting reset vectors for all harts to 0x%llx...\n", 
               (unsigned long long)load_addr);
        for (int hart = 0; hart < 4; hart++) {
            uint64_t rv_addr = L2CPU_EXT_PERIPH_BASE + (hart * 8);
            noc_write64(device, L2CPU0_X, L2CPU0_Y, rv_addr, load_addr);
        }
        
        // Verify reset vector
        uint64_t rv = noc_read64(device, L2CPU0_X, L2CPU0_Y, RESET_VECTOR_HART0);
        printf("  Reset vector readback: 0x%llx\n", (unsigned long long)rv);
        
        // Check suppress instruction fetch flag (high 16 bits of 32-bit read at 0x0400)
        uint32_t status_word = device.noc_read32(L2CPU0_X, L2CPU0_Y, HART_STATUS_AND_SUPPRESS);
        uint16_t hart_status_pre = status_word & 0xFFFF;
        uint16_t suppress = (status_word >> 16) & 0xFFFF;
        printf("Pre-reset status: hart_status=0x%04x suppress=0x%04x\n", hart_status_pre, suppress);
        if (suppress != 0) {
            printf("  WARNING: Suppress fetch flags are set! Clearing...\n");
            // Write 0 to the suppress field (high 16 bits)
            device.noc_write32(L2CPU0_X, L2CPU0_Y, HART_STATUS_AND_SUPPRESS, hart_status_pre);
        }
        
        // Read current L2CPU reset state
        uint32_t reset_reg = device.noc_read32(ARC_X, ARC_Y, ARC_L2CPU_RESET);
        printf("L2CPU_RESET register: 0x%08x\n", reset_reg);
        
        // Check if L2CPU0 is already out of reset (bit 4)
        if (reset_reg & (1 << 4)) {
            printf("WARNING: L2CPU0 is already out of reset!\n");
            printf("  The X280 can only be reset once per chip reset.\n");
            printf("  Run 'tt-smi -r 0' to reset the chip and try again.\n");
            
            // Let's still check the mailbox in case we're reusing a running firmware
            printf("\nChecking if firmware is already running...\n");
        } else {
            // === Critical boot sequence from tt-bh-linux ===
            
            // Step 1: Set clock to low speed (200 MHz)
            printf("Setting L2CPU clock to 200 MHz (low speed for reset)...\n");
            set_l2cpu_pll(device, PLL_200MHZ);
            
            // Step 2: Bring L2CPU0 out of reset (set bit 4)
            printf("Bringing L2CPU0 out of reset...\n");
            reset_reg |= (1 << 4);
            device.noc_write32(ARC_X, ARC_Y, ARC_L2CPU_RESET, reset_reg);
            
            // Readback to ensure write completed
            reset_reg = device.noc_read32(ARC_X, ARC_Y, ARC_L2CPU_RESET);
            printf("  L2CPU_RESET after: 0x%08x\n", reset_reg);
            
            // Step 3: Raise clock to full speed (1750 MHz)
            printf("Raising L2CPU clock to 1750 MHz...\n");
            set_l2cpu_pll(device, PLL_1750MHZ);
            
            // Step 4: Configure L2 prefetchers (recommended by SiFive)
            printf("Configuring L2 prefetchers...\n");
            configure_l2_prefetchers(device, L2CPU0_X, L2CPU0_Y);
            
            // Give firmware time to start
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        // Check hart status before polling
        printf("\nChecking hart status...\n");
        status_word = device.noc_read32(L2CPU0_X, L2CPU0_Y, HART_STATUS_AND_SUPPRESS);
        uint16_t hart_status = status_word & 0xFFFF;
        suppress = (status_word >> 16) & 0xFFFF;
        printf("  Status word: 0x%08x (hart_status=0x%04x, suppress=0x%04x)\n", 
               status_word, hart_status, suppress);
        printf("    Bits: cease=%d halt=%d wfi=%d debug=%d (per hart, 4 bits each)\n",
               (hart_status >> 0) & 0xF,
               (hart_status >> 4) & 0xF, 
               (hart_status >> 8) & 0xF,
               (hart_status >> 12) & 0xF);
        
        // Poll mailbox for magic value
        printf("\nPolling mailbox (0x%llx) for firmware response...\n", (unsigned long long)mailbox_addr);
        for (int attempt = 0; attempt < 20; attempt++) {
            uint64_t magic = noc_read64(device, L2CPU0_X, L2CPU0_Y, mailbox_addr);
            uint32_t magic32 = device.noc_read32(L2CPU0_X, L2CPU0_Y, mailbox_addr);
            uint64_t heartbeat = noc_read64(device, L2CPU0_X, L2CPU0_Y, mailbox_addr + 8);
            uint64_t hart_id = noc_read64(device, L2CPU0_X, L2CPU0_Y, mailbox_addr + 0x20);
            
            printf("  [%2d] magic=0x%016llx (lo32=0x%08x) heartbeat=%llu hart=%llu\n",
                   attempt,
                   (unsigned long long)magic,
                   magic32,
                   (unsigned long long)heartbeat,
                   (unsigned long long)hart_id);
            
            // Check for either 64-bit magic (hello.c) or 32-bit (minimal.S)
            if (magic == MAGIC_EXPECTED || magic32 == MAGIC_MINIMAL) {
                printf("\n*** SUCCESS! Firmware is running! ***\n\n");
                
                // Watch heartbeat for a bit
                printf("Watching heartbeat (Ctrl-C to stop):\n");
                uint64_t last_heartbeat = heartbeat;
                for (int i = 0; i < 10; i++) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                    heartbeat = noc_read64(device, L2CPU0_X, L2CPU0_Y, mailbox_addr + 8);
                    printf("  Heartbeat: %llu (+%llu)\n",
                           (unsigned long long)heartbeat,
                           (unsigned long long)(heartbeat - last_heartbeat));
                    last_heartbeat = heartbeat;
                }
                
                return 0;
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        
        // Final debug: read more status
        printf("\n*** FAILED: Firmware did not respond ***\n");
        printf("Expected magic: 0x%016llx or 0x%08x\n", 
               (unsigned long long)MAGIC_EXPECTED, MAGIC_MINIMAL);
        
        // Dump some debug info
        printf("\nDebug info:\n");
        status_word = device.noc_read32(L2CPU0_X, L2CPU0_Y, HART_STATUS_AND_SUPPRESS);
        printf("  Final status word: 0x%08x\n", status_word);
        printf("    hart_status=0x%04x suppress=0x%04x\n", 
               status_word & 0xFFFF, (status_word >> 16) & 0xFFFF);
        
        // Try reading the beginning of code to see if it's still there
        printf("  Code at 0x%llx:\n", (unsigned long long)load_addr);
        for (int i = 0; i < 8; i++) {
            uint32_t word = device.noc_read32(L2CPU0_X, L2CPU0_Y, load_addr + i*4);
            printf("    [%02x]: 0x%08x\n", i*4, word);
        }
        
        return 1;
        
    } catch (const std::exception& e) {
        fprintf(stderr, "Error: %s\n", e.what());
        return 1;
    }
    
    return 0;
}

