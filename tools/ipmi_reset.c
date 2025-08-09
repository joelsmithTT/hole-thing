// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
// SPDX-License-Identifier: GPL-2.0-only
//
// Reset Tool for Tenstorrent Devices via an external management controller.
//
// This tool orchestrates a reset sequence that involves an external tool
// like ipmitool to perform the actual hardware reset. It can target a
// single device or all devices found in /dev/tenstorrent.
// 
// Warning: AI-generated.
//
// To Compile:
//   gcc -o ipmi_reset ipmi_reset.c
//
// To Run (Single Device):
//   sudo ./ipmi_reset <device_id> <command_to_execute...>
//   Example: sudo ./ipmi_reset 0 sudo ipmitool <raw bytes>
//   CAUTION: You are on your own for associating device_id with the right byte sequence.
//
// To Run (All Devices):
//   sudo ./ipmi_reset -1 <command_to_execute...>
//   Example: sudo ./ipmi_reset -1 ipmitool raw 0x30 0x8B 0xF 0xFF 0x0 0xF
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <time.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <linux/types.h>

// --- Logging Macros ---
#define INFO(fmt, ...) do { fprintf(stdout, "[INFO] " fmt "\n", ##__VA_ARGS__); } while (0)
#define DEBUG(fmt, ...) do { if(getenv("DEBUG")) { fprintf(stdout, "[DEBUG] " fmt "\n", ##__VA_ARGS__); } } while (0)
#define FATAL(fmt, ...) do { fprintf(stderr, "[FATAL] %s:%d: " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__); exit(EXIT_FAILURE); } while (0)

// --- Driver & PCI Constants ---
#define TENSTORRENT_IOCTL_MAGIC 0xFA
#define TENSTORRENT_IOCTL_GET_DEVICE_INFO	_IO(TENSTORRENT_IOCTL_MAGIC, 0)
#define TENSTORRENT_IOCTL_RESET_DEVICE		_IO(TENSTORRENT_IOCTL_MAGIC, 6)
#define TENSTORRENT_RESET_DEVICE_USER_RESET 3
#define TENSTORRENT_RESET_DEVICE_POST_RESET 6
#define BDF_STRING_SIZE 18
#define PCI_CONFIG_COMMAND_OFFSET 0x04
#define PCI_CONFIG_SERR_ENABLE_BIT 6
#define MAX_SUPPORTED_DEVICES 256

// --- IOCTL Structures ---
struct tenstorrent_get_device_info_in { __u32 output_size_bytes; };
struct tenstorrent_get_device_info_out { __u32 output_size_bytes; __u16 vendor_id, device_id, subsystem_vendor_id, subsystem_id, bus_dev_fn, max_dma_buf_size_log2, pci_domain; };
struct tenstorrent_get_device_info { struct tenstorrent_get_device_info_in in; struct tenstorrent_get_device_info_out out; };
struct tenstorrent_reset_device_in { __u32 output_size_bytes; __u32 flags; };
struct tenstorrent_reset_device_out { __u32 output_size_bytes; __u32 result; };
struct tenstorrent_reset_device { struct tenstorrent_reset_device_in in; struct tenstorrent_reset_device_out out; };


// --- Forward Declarations ---
int get_bdf_for_dev_id(int dev_id, char *bdf_buf);
int find_dev_id_by_bdf(const char *target_bdf);
void execute_command(char **cmd_argv);
void wait_for_reset_completion(const char *pci_bdf);
int discover_all_devices(int **device_ids);
void perform_user_reset(int dev_id, char *bdf_out);
void perform_post_reset(const char *bdf);


int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <device_id|-1> <command_to_execute...>\n", argv[0]);
        return EXIT_FAILURE;
    }

    long target_dev_id = strtol(argv[1], NULL, 10);
    char **external_cmd = &argv[2];

    if (target_dev_id == -1) {
        // --- ALL DEVICES MODE ---
        INFO("Running in 'all devices' mode (-1).");
        int *device_ids = NULL;
        int device_count = discover_all_devices(&device_ids);
        if (device_count == 0) {
            INFO("No devices to reset.");
            free(device_ids);
            return EXIT_SUCCESS;
        }

        char (*bdf_list)[BDF_STRING_SIZE] = malloc(device_count * BDF_STRING_SIZE);
        if (!bdf_list) FATAL("Failed to allocate memory for BDF list.");

        // Step 1: Pre-reset all devices
        for (int i = 0; i < device_count; i++) {
            perform_user_reset(device_ids[i], bdf_list[i]);
        }

        // Step 2: Execute the single external command
        execute_command(external_cmd);

        // Step 3: Wait for completion on all devices
        for (int i = 0; i < device_count; i++) {
            wait_for_reset_completion(bdf_list[i]);
        }
        INFO("All devices have completed hardware reset.");

        // Step 4: Post-reset all devices
        for (int i = 0; i < device_count; i++) {
            perform_post_reset(bdf_list[i]);
        }

        free(device_ids);
        free(bdf_list);

    } else {
        // --- SINGLE DEVICE MODE ---
        INFO("Running in 'single device' mode for device ID %ld.", target_dev_id);
        char pci_bdf[BDF_STRING_SIZE];

        perform_user_reset(target_dev_id, pci_bdf);
        execute_command(external_cmd);
        wait_for_reset_completion(pci_bdf);
        perform_post_reset(pci_bdf);
    }

    INFO("Reset sequence completed successfully.");
    return EXIT_SUCCESS;
}


// --- Function Implementations ---

void perform_user_reset(int dev_id, char *bdf_out) {
    if (get_bdf_for_dev_id(dev_id, bdf_out) != 0) {
        FATAL("Could not get BDF for device ID %d.", dev_id);
    }
    INFO("Device %d (BDF: %s): Sending USER_RESET ioctl...", dev_id, bdf_out);

    char dev_path[PATH_MAX];
    snprintf(dev_path, sizeof(dev_path), "/dev/tenstorrent/%d", dev_id);
    int fd = open(dev_path, O_RDWR);
    if (fd < 0) FATAL("Could not open device %s: %s", dev_path, strerror(errno));

    struct tenstorrent_reset_device reset_cmd = {0};
    reset_cmd.in.flags = TENSTORRENT_RESET_DEVICE_USER_RESET;
    reset_cmd.in.output_size_bytes = sizeof(reset_cmd.out);

    if (ioctl(fd, TENSTORRENT_IOCTL_RESET_DEVICE, &reset_cmd) < 0) {
        close(fd);
        FATAL("USER_RESET ioctl failed on device %d: %s", dev_id, strerror(errno));
    }
    close(fd);
    if (reset_cmd.out.result != 0) {
        FATAL("USER_RESET ioctl on device %d returned error code %u", dev_id, reset_cmd.out.result);
    }
}

void perform_post_reset(const char *bdf) {
    INFO("Device (BDF: %s): Starting POST_RESET sequence...", bdf);
    const int chardev_timeout_s = 15;
    time_t start_time = time(NULL);
    int new_dev_id = -1;

    // Find the new character device ID for the BDF
    while (time(NULL) - start_time < chardev_timeout_s) {
        new_dev_id = find_dev_id_by_bdf(bdf);
        if (new_dev_id != -1) break;
        usleep(500000);
    }
    if (new_dev_id == -1) FATAL("Timed out finding character device for BDF %s after reset.", bdf);
    INFO("Device (BDF: %s): Found at new device ID %d.", bdf, new_dev_id);

    // Send the POST_RESET ioctl
    char dev_path[PATH_MAX];
    snprintf(dev_path, sizeof(dev_path), "/dev/tenstorrent/%d", new_dev_id);
    int fd = open(dev_path, O_RDWR);
    if (fd < 0) FATAL("Could not open re-discovered device %s: %s", dev_path, strerror(errno));

    struct tenstorrent_reset_device reset_cmd = {0};
    reset_cmd.in.flags = TENSTORRENT_RESET_DEVICE_POST_RESET;
    reset_cmd.in.output_size_bytes = sizeof(reset_cmd.out);

    if (ioctl(fd, TENSTORRENT_IOCTL_RESET_DEVICE, &reset_cmd) < 0) {
        close(fd);
        FATAL("POST_RESET ioctl failed on device %s: %s", bdf, strerror(errno));
    }
    close(fd);
    if (reset_cmd.out.result != 0) {
        FATAL("POST_RESET ioctl on device %s returned error code %u", bdf, reset_cmd.out.result);
    }
    INFO("Device (BDF: %s): POST_RESET sequence complete.", bdf);
}

int discover_all_devices(int **device_ids) {
    DIR *d = opendir("/dev/tenstorrent/");
    if (!d) FATAL("Could not open /dev/tenstorrent: %s", strerror(errno));

    *device_ids = malloc(sizeof(int) * MAX_SUPPORTED_DEVICES);
    if (!*device_ids) FATAL("Failed to allocate memory for device list");

    int count = 0;
    struct dirent *dir;
    while ((dir = readdir(d)) != NULL && count < MAX_SUPPORTED_DEVICES) {
        if (dir->d_type != DT_CHR) continue;
        char *endptr;
        long dev_id = strtol(dir->d_name, &endptr, 10);
        if (*endptr == '\0') {
            (*device_ids)[count++] = (int)dev_id;
        }
    }
    closedir(d);

    INFO("Discovered %d device(s) in /dev/tenstorrent.", count);
    return count;
}

int get_bdf_for_dev_id(int dev_id, char *bdf_buf) {
    char dev_path[PATH_MAX];
    snprintf(dev_path, sizeof(dev_path), "/dev/tenstorrent/%d", dev_id);
    int fd = open(dev_path, O_RDWR);
    if (fd < 0) return -1;
    struct tenstorrent_get_device_info info = {0};
    info.in.output_size_bytes = sizeof(info.out);
    if (ioctl(fd, TENSTORRENT_IOCTL_GET_DEVICE_INFO, &info) < 0) {
        close(fd); return -1;
    }
    close(fd);
    snprintf(bdf_buf, BDF_STRING_SIZE, "%04x:%02x:%02x.%x",
             info.out.pci_domain, (info.out.bus_dev_fn >> 8) & 0xFF,
             (info.out.bus_dev_fn >> 3) & 0x1F, info.out.bus_dev_fn & 0x7);
    return 0;
}

int find_dev_id_by_bdf(const char *target_bdf) {
    DIR *d = opendir("/dev/tenstorrent/");
    if (!d) return -1;
    struct dirent *dir;
    int found_id = -1;
    while ((dir = readdir(d)) != NULL) {
        if (dir->d_type != DT_CHR) continue;
        char *endptr;
        long dev_id = strtol(dir->d_name, &endptr, 10);
        if (*endptr != '\0') continue;
        char current_bdf[BDF_STRING_SIZE];
        if (get_bdf_for_dev_id((int)dev_id, current_bdf) == 0) {
            if (strcmp(target_bdf, current_bdf) == 0) {
                found_id = (int)dev_id;
                break;
            }
        }
    }
    closedir(d);
    return found_id;
}

void execute_command(char **cmd_argv) {
    INFO("Executing external command: `%s ...`", cmd_argv[0]);
    pid_t pid = fork();
    if (pid == -1) FATAL("Failed to fork: %s", strerror(errno));
    else if (pid == 0) {
        execvp(cmd_argv[0], cmd_argv);
        fprintf(stderr, "Failed to execute '%s': %s\n", cmd_argv[0], strerror(errno));
        exit(127);
    } else {
        int status;
        waitpid(pid, &status, 0);
        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            INFO("External command executed successfully.");
        } else {
            FATAL("External command failed with status %d.", WEXITSTATUS(status));
        }
    }
}

void wait_for_reset_completion(const char *pci_bdf) {
    char config_path[PATH_MAX];
    snprintf(config_path, sizeof(config_path), "/sys/bus/pci/devices/%s/config", pci_bdf);
    const int timeout_s = 60;
    time_t start_time = time(NULL);
    INFO("Device (BDF: %s): Waiting for reset completion...", pci_bdf);

    while (time(NULL) - start_time < timeout_s) {
        int config_fd = open(config_path, O_RDONLY);
        if (config_fd < 0) {
            DEBUG("Device (BDF: %s): PCI config inaccessible (%s). Retrying...", pci_bdf, strerror(errno));
            usleep(200000);
            continue;
        }
        char reg_val;
        if (pread(config_fd, &reg_val, 1, PCI_CONFIG_COMMAND_OFFSET) == 1) {
            if (((reg_val >> PCI_CONFIG_SERR_ENABLE_BIT) & 1) == 0) {
                close(config_fd);
                INFO("Device (BDF: %s): Reset marker bit is clear. Reset complete.", pci_bdf);
                return;
            }
        }
        close(config_fd);
        usleep(200000);
    }
    FATAL("Device (BDF: %s): Timeout waiting for reset marker to clear.", pci_bdf);
}
