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

// Memory layout (must match tensix/iter01.c)
static constexpr uint64_t COUNTER_ADDR = 0x1000;
static constexpr uint64_t CONTROL_ADDR = 0x1004;
static constexpr uint64_t MARKER_ADDR  = 0x1008;

static constexpr uint32_t CONTROL_RUN  = 0x00000000;
static constexpr uint32_t CONTROL_STOP = 0xDEADC0DE;

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

    // Load program
    std::vector<uint8_t> program = read_bin("tensix/iter01.bin");
    std::cout << "\n=== Iteration 01: Counter with Stop Control ===\n";
    std::cout << "Program size: " << program.size() << " bytes\n\n";

    // Reset Tensix
    std::cout << "1. Resetting Tensix...\n";
    device.noc_write32(TENSIX_X, TENSIX_Y, TENSIX_RESET_REG, TENSIX_IN_RESET);

    // Load program
    std::cout << "2. Loading program...\n";
    device.noc_write(TENSIX_X, TENSIX_Y, 0x0, program.data(), program.size());

    // Initialize memory
    std::cout << "3. Initializing memory...\n";
    device.noc_write32(TENSIX_X, TENSIX_Y, COUNTER_ADDR, 0);
    device.noc_write32(TENSIX_X, TENSIX_Y, CONTROL_ADDR, CONTROL_RUN);
    device.noc_write32(TENSIX_X, TENSIX_Y, MARKER_ADDR, 0);

    // Start Tensix
    std::cout << "4. Starting Tensix...\n";
    device.noc_write32(TENSIX_X, TENSIX_Y, TENSIX_RESET_REG, TENSIX_OUT_RESET);
    usleep(10000);  // 10ms startup delay
    
    // Check marker
    uint32_t marker = device.noc_read32(TENSIX_X, TENSIX_Y, MARKER_ADDR);
    std::cout << "   Marker after start: 0x" << std::hex << marker << std::dec;
    if (marker == 0xABCD1234) {
        std::cout << " [STARTED]\n";
    } else {
        std::cout << " [NOT STARTED?]\n";
    }
    
    // Observe counter incrementing
    std::cout << "5. Observing counter (should increment)...\n";
    for (int i = 0; i < 10; i++) {
        uint32_t value = device.noc_read32(TENSIX_X, TENSIX_Y, COUNTER_ADDR);
        uint32_t ctrl = device.noc_read32(TENSIX_X, TENSIX_Y, CONTROL_ADDR);
        uint32_t mark = device.noc_read32(TENSIX_X, TENSIX_Y, MARKER_ADDR);
        std::cout << "   [" << i << "] Counter = " << std::dec << value 
                  << ", Control = 0x" << std::hex << ctrl 
                  << ", Marker = 0x" << mark << std::dec << "\n";
        usleep(100000);  // 100ms
    }

    // Send stop command
    std::cout << "6. Sending stop command (0x" << std::hex << CONTROL_STOP << std::dec << ")...\n";
    device.noc_write32(TENSIX_X, TENSIX_Y, CONTROL_ADDR, CONTROL_STOP);
    usleep(100000);  // 100ms

    // Read final counter
    uint32_t final_count = device.noc_read32(TENSIX_X, TENSIX_Y, COUNTER_ADDR);
    std::cout << "7. Final counter value: " << final_count << "\n";

    // Verify counter stopped changing
    usleep(200000);
    uint32_t check_count = device.noc_read32(TENSIX_X, TENSIX_Y, COUNTER_ADDR);
    std::cout << "8. Verify stopped: " << check_count;
    if (check_count == final_count) {
        std::cout << " [STOPPED - SUCCESS!]\n";
    } else {
        std::cout << " [STILL RUNNING - FAILURE!]\n";
    }

    // Reset
    device.noc_write32(TENSIX_X, TENSIX_Y, TENSIX_RESET_REG, TENSIX_IN_RESET);
    std::cout << "\nDone.\n";

    return 0;
}

