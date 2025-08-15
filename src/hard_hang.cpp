/**
 * SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * @file hang.cpp
 * @brief Hang the chip NOC
 *
 * NB: usually we DON'T want to hang the NOC.
 */
#include "holething.hpp"

#include <string>

using namespace tt;


// Contains the logic to hang the NOC for a single device.
// Returns 0 on success (NOC hung), 1 on failure.
int hang_device_noc(const std::string& device_path) {
    try {
        Device device(device_path.c_str());
        printf("--- Processing device: %s ---\n", device.get_path().c_str());

        // 1. Check if NOC is already hung before we do anything.
        if (DeviceUtils::noc_sanity_check(device)!= 0) {
            fprintf(stderr, "%s: NOC already appears to be hung. Skipping.\n", device.get_path().c_str());
            return 1; // Indicate failure/skip
        }

        // 2. Attempt to hang the NOC.
        TlbWindow tlb1(device, 1ULL << 21, TT_MMIO_CACHE_MODE_UC);

        // This recipe is from syseng, and results in a WH chip hang that seems
        // to require a secondary bus reset (performed by the driver) to get the
        // system into a state where initiating the reset is actually possible.
        tlb1.map(1, 11, 0xffa00000);
        for (uint32_t i = 0; i < 20; ++i) {
            tlb1.read32(0x114000 + (i * 4));
        }

        // Leftover from soft_hang.cpp copy/paste.
        bool timeout_triggered = false;
        for (int x = 0; x < 32; ++x) {
            for (int y = 0; y < 32; ++y) {
                tlb1.map(x, y, 0x0);
                // A read that times out returns 0xffffffff
                if (tlb1.read32(0) == 0xffffffff) {
                    timeout_triggered = true;
                    break;
                }
            }
            if (timeout_triggered) {
                break;
            }
        }

        // 3. Verify that the NOC is now unresponsive.
        if (DeviceUtils::noc_sanity_check(device) == 0) {
            fprintf(stderr, "%s: Failed to hang the NOC.\n", device.get_path().c_str());
            return 1; // Indicate failure
        }

        printf("%s: NOC successfully hung.\n", device.get_path().c_str());
        printf("%s: You probably want to reset the device now.\n", device.get_path().c_str());
        return 0; // Indicate success

    } catch (const std::runtime_error& e) {
        fprintf(stderr, "Error accessing device %s: %s\n", device_path.c_str(), e.what());
        return 1; // Indicate failure
    }
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
        printf("Running NOC hang test on all %zu devices...\n\n", device_paths.size());
        for (const auto& path : device_paths) {
            if (hang_device_noc(path) != 0) {
                failures++;
            }
            printf("\n"); // Add a newline for better readability between devices
        }

        if (failures > 0) {
            fprintf(stderr, "Finished. %d of %zu devices could not be hung or were skipped.\n", failures, device_paths.size());
            return 1;
        } else {
            printf("Finished. All devices were processed successfully.\n");
            return 0;
        }

    } else {
        // Run on a single, specified device.
        std::string device_path = "/dev/tenstorrent/" + arg;
        return hang_device_noc(device_path);
    }
}

