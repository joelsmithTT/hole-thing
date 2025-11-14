/**
 * SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * @file bar_zap_test.c
 * @brief BAR Mapping Zap Test - Tests BAR0 behavior when driver invalidates mappings
 *
 * WHAT THIS TOOL DOES:
 *   This tool maps BAR0 (the device's PCI Base Address Register 0) and continuously
 *   reads the PCIe coordinate register to verify device connectivity. It is designed
 *   to test how the driver and kernel handle BAR mappings when the device is reset
 *   externally, causing the driver to zap (invalidate) all device memory mappings.
 *   The tool catches SIGBUS and SIGINT to gracefully test cleanup paths (munmap).
 *
 * HOW TO BUILD:
 *   gcc -o bar_zap_test bar_zap_test.c
 *
 * HOW TO RUN:
 *   1. Run the tool in one terminal:
 *      ./bar_zap_test [/dev/tenstorrent/0]
 *   
 *   2. While the tool is running and printing PCIe coordinate reads, trigger a
 *      device reset from another terminal:
 *      tt-smi -r 0
 *   
 *   3. Observe the tool's behavior:
 *      - It should catch SIGBUS when the BAR mapping is invalidated
 *      - It will jump to cleanup and test munmap() behavior
 *      - Check whether munmap succeeds or fails with specific errors
 *   
 *   You can also press Ctrl+C to test normal cleanup without a reset.
 *
 * REQUIREMENTS:
 *   - Blackhole device
 *   - tt-kmd driver with mapping zap support
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <setjmp.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <errno.h>
#include <linux/types.h>

// ===== Copied from ioctl.h =====

#define TENSTORRENT_IOCTL_MAGIC 0xFA

#define TENSTORRENT_IOCTL_GET_DEVICE_INFO	_IO(TENSTORRENT_IOCTL_MAGIC, 0)
#define TENSTORRENT_IOCTL_QUERY_MAPPINGS	_IO(TENSTORRENT_IOCTL_MAGIC, 2)

#define TENSTORRENT_MAPPING_UNUSED		0
#define TENSTORRENT_MAPPING_RESOURCE0_UC	1
#define TENSTORRENT_MAPPING_RESOURCE0_WC	2
#define TENSTORRENT_MAPPING_RESOURCE1_UC	3
#define TENSTORRENT_MAPPING_RESOURCE1_WC	4
#define TENSTORRENT_MAPPING_RESOURCE2_UC	5
#define TENSTORRENT_MAPPING_RESOURCE2_WC	6

struct tenstorrent_get_device_info_in {
	__u32 output_size_bytes;
};

struct tenstorrent_get_device_info_out {
	__u32 output_size_bytes;
	__u16 vendor_id;
	__u16 device_id;
	__u16 subsystem_vendor_id;
	__u16 subsystem_id;
	__u16 bus_dev_fn;
	__u16 max_dma_buf_size_log2;
	__u16 pci_domain;
};

struct tenstorrent_get_device_info {
	struct tenstorrent_get_device_info_in in;
	struct tenstorrent_get_device_info_out out;
};

struct tenstorrent_query_mappings_in {
	__u32 output_mapping_count;
	__u32 reserved;
};

struct tenstorrent_mapping {
	__u32 mapping_id;
	__u32 reserved;
	__u64 mapping_base;
	__u64 mapping_size;
};

struct tenstorrent_query_mappings_out {
	struct tenstorrent_mapping mappings[0];
};

struct tenstorrent_query_mappings {
	struct tenstorrent_query_mappings_in in;
	struct tenstorrent_query_mappings_out out;
};

// ===== End of ioctl.h copy =====

// PCIe coordinate register offset
#define NOC_ID_OFFSET 0x4044
#define NOC2AXI_CFG_START 0x1FD00000
#define PCIE_COORD_OFFSET (NOC2AXI_CFG_START + NOC_ID_OFFSET)

// Helper struct for TENSTORRENT_IOCTL_QUERY_MAPPINGS
struct tenstorrent_query_mappings_flex {
    struct tenstorrent_query_mappings_in in;
    struct tenstorrent_mapping out_mappings[16];
};

// Jump buffer for signal recovery
static sigjmp_buf signal_jmp_buf;

// Signal handler for SIGBUS and SIGINT
static void signal_handler(int sig)
{
    const char *signame = (sig == SIGBUS) ? "SIGBUS" : (sig == SIGINT) ? "SIGINT" : "signal";
    printf("\n*** %s caught (signal %d) in PID %d - jumping to cleanup ***\n", signame, sig, getpid());
    fflush(stdout);
    siglongjmp(signal_jmp_buf, 1);
}

int main(int argc, char *argv[])
{
    const char *device_path = "/dev/tenstorrent/0";
    int fd = -1;
    void *bar0 = MAP_FAILED;
    uint64_t bar0_offset = 0;
    uint64_t bar0_size = 0;
    int r;
    struct tenstorrent_get_device_info dev_info;
    struct sigaction sa;
    struct tenstorrent_query_mappings_flex mappings_query;
    volatile uint32_t *coord_reg;
    int iteration;
    int found;
    
    if (argc > 1) {
        device_path = argv[1];
    }

    // Open device
    fd = open(device_path, O_RDWR | O_SYNC);
    if (fd < 0) {
        fprintf(stderr, "Failed to open device %s: %s\n", device_path, strerror(errno));
        return 1;
    }
    printf("Opened device: %s (fd=%d)\n", device_path, fd);

    // Get device info
    memset(&dev_info, 0, sizeof(dev_info));
    dev_info.in.output_size_bytes = sizeof(dev_info.out);

    r = ioctl(fd, TENSTORRENT_IOCTL_GET_DEVICE_INFO, &dev_info);
    if (r != 0) {
        perror("ioctl GET_DEVICE_INFO");
        close(fd);
        return 1;
    }
    printf("Device: vendor=0x%04x device=0x%04x\n", 
           dev_info.out.vendor_id, dev_info.out.device_id);

    // Check if Blackhole (device_id 0xB140)
    if (dev_info.out.device_id != 0xB140) {
        fprintf(stderr, "Error: This tool requires Blackhole (found device_id=0x%04x)\n",
                dev_info.out.device_id);
        close(fd);
        return 1;
    }
    printf("Device is Blackhole\n");

    // Install signal handlers for SIGBUS and SIGINT
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    
    if (sigaction(SIGBUS, &sa, NULL) != 0) {
        perror("sigaction SIGBUS");
        close(fd);
        return 1;
    }
    
    if (sigaction(SIGINT, &sa, NULL) != 0) {
        perror("sigaction SIGINT");
        close(fd);
        return 1;
    }
    printf("Installed SIGBUS and SIGINT handlers\n");

    // Query mappings to find BAR0
    memset(&mappings_query, 0, sizeof(mappings_query));
    mappings_query.in.output_mapping_count = 16;

    r = ioctl(fd, TENSTORRENT_IOCTL_QUERY_MAPPINGS, &mappings_query);
    if (r != 0) {
        perror("ioctl QUERY_MAPPINGS");
        close(fd);
        return 1;
    }

    found = 0;
    for (int i = 0; i < 16; i++) {
        if (mappings_query.out_mappings[i].mapping_id == TENSTORRENT_MAPPING_RESOURCE0_UC) {
            bar0_offset = mappings_query.out_mappings[i].mapping_base;
            bar0_size = mappings_query.out_mappings[i].mapping_size;
            found = 1;
            break;
        }
    }

    if (!found || bar0_size == 0) {
        fprintf(stderr, "Could not find BAR0 (uncached) mapping\n");
        close(fd);
        return 1;
    }
    printf("Found BAR0: offset=0x%llx, size=0x%llx\n",
           (unsigned long long)bar0_offset, (unsigned long long)bar0_size);

    // mmap BAR0
    bar0 = mmap(NULL, bar0_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, bar0_offset);
    if (bar0 == MAP_FAILED) {
        perror("mmap");
        goto cleanup;
    }
    printf("Mapped BAR0 at %p\n", bar0);

    // Get pointer to PCIe coordinate register
    coord_reg = (volatile uint32_t *)((uint8_t *)bar0 + PCIE_COORD_OFFSET);
    printf("PCIe coordinate register at offset 0x%x\n", PCIE_COORD_OFFSET);

    // Set up longjmp target for signal handlers
    if (sigsetjmp(signal_jmp_buf, 1) != 0) {
        // Returned from signal handler via siglongjmp
        printf("Resumed execution after signal\n");
        goto cleanup;
    }

    printf("\nStarting continuous read loop (Ctrl+C to exit)...\n\n");
    
    iteration = 0;
    while (1) {
        uint32_t coords = *coord_reg;
        
        // Extract X and Y coordinates
        uint32_t x_coord = (coords >> 0x0) & 0x3f;
        uint32_t y_coord = (coords >> 0x6) & 0x3f;

        printf("[%d] Read PCIe coords: 0x%08x -> X=%u Y=%u\n",
               iteration, coords, x_coord, y_coord);

        fflush(stdout);
        iteration++;
        usleep(100000);
    }

cleanup:
    printf("\nCleaning up...\n");

    // Test munmap when mappings might be zapped
    if (bar0 != MAP_FAILED) {
        printf("Calling munmap(%p, %llu)...\n", bar0, (unsigned long long)bar0_size);
        r = munmap(bar0, bar0_size);
        if (r != 0) {
            printf("  munmap failed: %s\n", strerror(errno));
        } else {
            printf("  munmap succeeded\n");
        }
    }

    if (fd >= 0) {
        printf("Closing device fd=%d...\n", fd);
        close(fd);
        printf("  close succeeded\n");
    }

    printf("Cleanup complete\n");
    return 0;
}

