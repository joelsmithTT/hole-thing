// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
// SPDX-License-Identifier: GPL-2.0-only
//
// Power state Tool for Tenstorrent Devices.
//
// To Compile:
//  gcc -o power power.c
//
// To Run:
//  ./power <device_id> <command>
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <linux/types.h>

// --- Logging Macros ---
#define INFO(fmt, ...) do { fprintf(stdout, "%s:%d: " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__); } while (0)
#define FATAL(fmt, ...) do { fprintf(stderr, "%s:%d: " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__); exit(1); } while (0)

#define TENSTORRENT_IOCTL_MAGIC 0xFA
#define TENSTORRENT_IOCTL_SET_POWER_STATE _IO(TENSTORRENT_IOCTL_MAGIC, 15)

struct tenstorrent_power_state {
	__u32 argsz;
	__u32 flags;
	__u8 reserved0;
	__u8 validity;
#define TT_POWER_VALIDITY_FLAGS(n)      (((n) & 0xF) << 0)
#define TT_POWER_VALIDITY_SETTINGS(n)   (((n) & 0xF) << 4)
#define TT_POWER_VALIDITY(flags, settings) (TT_POWER_VALIDITY_FLAGS(flags) | TT_POWER_VALIDITY_SETTINGS(settings))
	__u16 power_flags;
#define TT_POWER_FLAG_MAX_AI_CLK        (1U << 0) /* 1=Max AI Clock, 0=Min AI Clock */
#define TT_POWER_FLAG_MRISC_PHY_WAKEUP  (1U << 1) /* 1=PHY Wakeup,   0=PHY Powerdown */
	__u16 power_settings[14];
};


static void set_power_state(int fd, __u16 power_flags, __u8 num_flags) {
    struct tenstorrent_power_state power_state = {0};

    power_state.argsz = sizeof(power_state);
    power_state.validity = TT_POWER_VALIDITY(num_flags, 0);
    power_state.power_flags = power_flags;

    INFO("Setting power state with flags: 0x%04X, validity: 0x%02X",
         power_state.power_flags, power_state.validity);

    if (ioctl(fd, TENSTORRENT_IOCTL_SET_POWER_STATE, &power_state) < 0) {
        FATAL("Failed to set power state: %s", strerror(errno));
    }

    INFO("Successfully set power state.");
}

void print_usage(const char *exec_name) {
    fprintf(stderr, "Usage: %s <device_id> <command>\n", exec_name);
    fprintf(stderr, "Commands:\n");
    fprintf(stderr, "  max_ai_low_phy   - Set max AI clock and low MRISC PHY power.\n");
    fprintf(stderr, "  min_ai_max_phy   - Set min AI clock and max MRISC PHY power.\n");
    fprintf(stderr, "  max_ai_max_phy   - Set max AI clock and max MRISC PHY power.\n");
    fprintf(stderr, "  min_ai_low_phy   - Set min AI clock and low MRISC PHY power.\n");
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        print_usage(argv[0]);
        exit(1);
    }

    int dev_id = atoi(argv[1]);
    const char *command = argv[2];

    char dev_path[128];
    snprintf(dev_path, sizeof(dev_path), "/dev/tenstorrent/%d", dev_id);

    int fd = open(dev_path, O_RDWR|O_APPEND);
    if (fd < 0) {
        FATAL("Could not open device %s: %s", dev_path, strerror(errno));
    }

    __u16 power_flags = 0;
    __u8 num_flags = 2; // We are controlling 2 flags.

    if (strcmp(command, "max_ai_low_phy") == 0) {
        power_flags = TT_POWER_FLAG_MAX_AI_CLK;
    } else if (strcmp(command, "min_ai_max_phy") == 0) {
        power_flags = TT_POWER_FLAG_MRISC_PHY_WAKEUP;
    } else if (strcmp(command, "max_ai_max_phy") == 0) {
        power_flags = TT_POWER_FLAG_MAX_AI_CLK | TT_POWER_FLAG_MRISC_PHY_WAKEUP;
    } else if (strcmp(command, "min_ai_low_phy") == 0) {
        power_flags = 0;
    } else {
        fprintf(stderr, "Unknown command: %s\n", command);
        print_usage(argv[0]);
        close(fd);
        exit(1);
    }

    set_power_state(fd, power_flags, num_flags);

    close(fd);
    return 0;
}
