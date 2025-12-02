#include "holething.hpp"

#include <iostream>
#include <iomanip>
#include <fstream>
#include <vector>
#include <unistd.h>
#include <csignal>

using namespace tt;

// Tensix reset register
static constexpr uint64_t TENSIX_REG_BASE   = 0xFFB00000;
static constexpr uint64_t TENSIX_RESET_REG  = TENSIX_REG_BASE + 0x121B0;
static constexpr uint32_t TENSIX_IN_RESET   = 0x47800;
static constexpr uint32_t TENSIX_OUT_RESET  = 0x47000;

// GDDR target (where Tensix writes the counter)
static constexpr uint8_t GDDR_X = 17;
static constexpr uint8_t GDDR_Y = 12;
static constexpr uint64_t GDDR_COUNTER_ADDR = 0x0;

static volatile bool running = true;

void signal_handler(int sig)
{
    (void)sig;
    running = false;
}

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

    // Setup signal handler for clean exit
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    Device device("/dev/tenstorrent/0");
    DeviceUtils::print_device_info(device);

    constexpr uint8_t TENSIX_X = 2;
    constexpr uint8_t TENSIX_Y = 2;

    std::cout << "\n=== Iteration 06: Free-Running Counter to GDDR ===\n";
    std::cout << "Tensix writes cycle counter to GDDR(" << (int)GDDR_X << "," << (int)GDDR_Y << ") @ 0x0\n";
    std::cout << "Host reads counter from GDDR\n";
    std::cout << "Press Ctrl+C to stop\n\n";

    // Load program
    std::vector<uint8_t> program = read_bin("tensix/iter06.bin");
    std::cout << "1. Loading Tensix program (" << program.size() << " bytes)...\n";

    // Reset Tensix
    device.noc_write32(TENSIX_X, TENSIX_Y, TENSIX_RESET_REG, TENSIX_IN_RESET);

    // Load program
    device.noc_write(TENSIX_X, TENSIX_Y, 0x0, program.data(), program.size());

    // Start Tensix
    std::cout << "2. Starting Tensix...\n";
    device.noc_write32(TENSIX_X, TENSIX_Y, TENSIX_RESET_REG, TENSIX_OUT_RESET);

    // Small delay to let Tensix start
    usleep(10000);

    // Read counter from GDDR in a loop
    std::cout << "3. Reading counter from GDDR (Ctrl+C to stop)...\n\n";

    uint32_t prev_counter = 0;
    uint64_t reads = 0;

    while (running) {
        uint32_t counter = device.noc_read32(GDDR_X, GDDR_Y, GDDR_COUNTER_ADDR);
        reads++;

        // Calculate delta (handles wrap)
        uint32_t delta = counter - prev_counter;

        std::cout << "\r  Counter: " << std::setw(12) << counter
                  << "  Delta: " << std::setw(10) << delta
                  << "  Reads: " << std::setw(8) << reads
                  << std::flush;

        prev_counter = counter;
        usleep(100000);  // 100ms between reads
    }

    std::cout << "\n\n4. Stopping...\n";

    // Reset Tensix
    device.noc_write32(TENSIX_X, TENSIX_Y, TENSIX_RESET_REG, TENSIX_IN_RESET);

    std::cout << "Done.\n";
    return 0;
}

