/**
 * SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * @file scratch.cpp
 * @brief Look at the scratch registers.
 * @warning: AI-generated code.
 *
 * Last time this was used, it was for debugging why I was reading zeros from
 * the telemetry scratch registers in KMD.
 */

#include "holething.hpp"

#include <iostream>
#include <string>
#include <chrono>
#include <thread>

using namespace tt;

int main_ai_v2(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <device_number>" << std::endl;
        return 1;
    }

    std::string device_path = "/dev/tenstorrent/";
    device_path += std::string(argv[1]);

    Device* device = nullptr;
    auto start_time = std::chrono::steady_clock::now();
    unsigned int last_value = 0; // Stores the last read value to check for changes
    bool first_read = true;      // A flag to ensure the first value is always printed

    std::cout << "Starting device monitor. Will only print on value changes.\n";
    std::cout << "Exit condition: Scratch[0] & 0xFF == 0x42\n" << std::endl;

    for (;;) {
        try {
            if (!device) {
                // This block handles both initial connection and reconnection
                device = new Device(device_path.c_str());
                std::cout << "--> Device connected on " << device_path << ". Monitoring..." << std::endl;
                start_time = std::chrono::steady_clock::now();
                first_read = true; // Reset state to ensure the first value prints after reconnect
            }

            unsigned int current_value = device->read_scratch(0);

            // ✅ 1. Only print if the value has changed
            if (first_read || current_value != last_value) {
                auto elapsed = std::chrono::steady_clock::now() - start_time;
                auto minutes = std::chrono::duration_cast<std::chrono::minutes>(elapsed);
                auto seconds = std::chrono::duration_cast<std::chrono::seconds>(elapsed % std::chrono::minutes(1));
                auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed % std::chrono::seconds(1));

                std::cout << std::setfill('0')
                          << std::setw(2) << minutes.count() << ":"
                          << std::setw(2) << seconds.count() << "."
                          << std::setw(3) << milliseconds.count()
                          << "  |  Scratch[0]: 0x" << std::hex << current_value << std::dec
                          << std::endl;

                last_value = current_value;
                first_read = false;
            }

            // ✅ 2. Check for the exit condition
            if ((current_value & 0xFF) == 0x42) {
                std::cout << "--> Exit condition met (0x" << std::hex << current_value << "). Shutting down." << std::dec << std::endl;
                break; // Exit the loop
            }

        } catch (const std::exception& e) {
            if (device != nullptr) {
                std::cerr << "--> Device disconnected. Error: " << e.what() << std::endl;
                delete device;
                device = nullptr;
            }
            // ✅ 3. Aggressively retry on failure
            std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Very short wait
            continue;
        }

        // Normal sleep interval between successful reads
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }

    std::cout << "Monitor stopped." << std::endl;
    delete device;
    return 0;
}

int main_ai_v3(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <device_number>" << std::endl;
        return 1;
    }

    std::string device_path = "/dev/tenstorrent/";
    device_path += std::string(argv[1]);

    Device* device = nullptr;
    auto start_time = std::chrono::steady_clock::now();
    unsigned int last_value = 0;
    bool first_read = true;

    // ✅ 1. Counter for the exit condition
    int exit_condition_count = 0;
    const int EXIT_THRESHOLD = 20000;

    std::cout << "Starting device monitor. Will only print on value changes.\n";
    std::cout << "Exit condition: " << EXIT_THRESHOLD
              << " non-consecutive reads of Scratch[0] & 0xFF == 0x42\n" << std::endl;

    for (;;) {
        try {
            if (!device) {
                device = new Device(device_path.c_str());
                std::cout << "--> Device connected on " << device_path << ". Monitoring..." << std::endl;
                start_time = std::chrono::steady_clock::now();
                first_read = true;
            }

            unsigned int current_value = device->read_scratch(0);

            if (first_read || current_value != last_value) {
                auto elapsed = std::chrono::steady_clock::now() - start_time;
                auto minutes = std::chrono::duration_cast<std::chrono::minutes>(elapsed);
                auto seconds = std::chrono::duration_cast<std::chrono::seconds>(elapsed % std::chrono::minutes(1));
                auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed % std::chrono::seconds(1));

                std::cout << std::setfill('0')
                          << std::setw(2) << minutes.count() << ":"
                          << std::setw(2) << seconds.count() << "."
                          << std::setw(3) << milliseconds.count()
                          << "  |  Scratch[0]: 0x" << std::hex << current_value << std::dec
                          << std::endl;

                last_value = current_value;
                first_read = false;
            }

            // ✅ 2. Check for the exit value and update the counter
            if ((current_value & 0xFF) == 0x42) {
                exit_condition_count++;
                // Provide feedback on progress towards the exit goal
                std::cout << "--> Exit value detected (seen " << exit_condition_count << "/" << EXIT_THRESHOLD << "). Value: 0x"
                          << std::hex << current_value << std::dec << std::endl;

                if (exit_condition_count >= EXIT_THRESHOLD) {
                    std::cout << "--> Exit threshold reached. Shutting down." << std::endl;
                    break; // Exit the loop
                }
            }

        } catch (const std::exception& e) {
            if (device != nullptr) {
                std::cerr << "--> Device disconnected. Error: " << e.what() << std::endl;
                delete device;
                device = nullptr;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
    }

    std::cout << "Monitor stopped." << std::endl;
    delete device;
    return 0;
}

int main(int argc, char** argv) {
    return main_ai_v3(argc, argv);
}