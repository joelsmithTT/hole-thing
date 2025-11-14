/**
 * SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * @file tlb_zap_test.c
 * @brief TLB Window Zap Test - Tests TLB behavior when driver invalidates mappings
 *
 * WHAT THIS TOOL DOES:
 *   This tool allocates a TLB window, maps it to a Tensix NOC coordinate, and
 *   continuously reads the NOC_NODE_ID register. It is designed to test how the
 *   driver and kernel handle TLB mappings when the device is reset externally,
 *   causing the driver to zap (invalidate) all device memory mappings. The tool
 *   catches SIGBUS and SIGINT to gracefully test cleanup paths (munmap, FREE_TLB).
 *
 * HOW TO BUILD:
 *   gcc -o tlb_zap_test tlb_zap_test.c
 *
 * HOW TO RUN:
 *   1. Run the tool in one terminal:
 *      ./tlb_zap_test [/dev/tenstorrent/0]
 *   
 *   2. While the tool is running and printing NOC reads, trigger a device reset
 *      from another terminal:
 *      tt-smi -r 0
 *   
 *   3. Observe the tool's behavior:
 *      - It should catch SIGBUS when the TLB mapping is invalidated
 *      - It will jump to cleanup and test munmap() and FREE_TLB ioctl behavior
 *      - Check whether these operations succeed or fail with specific errors
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
#define TENSTORRENT_IOCTL_ALLOCATE_TLB		_IO(TENSTORRENT_IOCTL_MAGIC, 11)
#define TENSTORRENT_IOCTL_FREE_TLB		_IO(TENSTORRENT_IOCTL_MAGIC, 12)
#define TENSTORRENT_IOCTL_CONFIGURE_TLB		_IO(TENSTORRENT_IOCTL_MAGIC, 13)

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

struct tenstorrent_allocate_tlb_in {
	__u64 size;
	__u64 reserved;
};

struct tenstorrent_allocate_tlb_out {
	__u32 id;
	__u32 reserved0;
	__u64 mmap_offset_uc;
	__u64 mmap_offset_wc;
	__u64 reserved1;
};

struct tenstorrent_allocate_tlb {
	struct tenstorrent_allocate_tlb_in in;
	struct tenstorrent_allocate_tlb_out out;
};

struct tenstorrent_free_tlb_in {
	__u32 id;
};

struct tenstorrent_free_tlb_out {
};

struct tenstorrent_free_tlb {
	struct tenstorrent_free_tlb_in in;
	struct tenstorrent_free_tlb_out out;
};

struct tenstorrent_noc_tlb_config {
	__u64 addr;
	__u16 x_end;
	__u16 y_end;
	__u16 x_start;
	__u16 y_start;
	__u8 noc;
	__u8 mcast;
	__u8 ordering;
	__u8 linked;
	__u8 static_vc;
	__u8 reserved0[3];
	__u32 reserved1[2];
};

struct tenstorrent_configure_tlb_in {
	__u32 id;
	struct tenstorrent_noc_tlb_config config;
};

struct tenstorrent_configure_tlb_out {
	__u64 reserved;
};

struct tenstorrent_configure_tlb {
	struct tenstorrent_configure_tlb_in in;
	struct tenstorrent_configure_tlb_out out;
};

// ===== End of ioctl.h copy =====

// Blackhole specific constants
#define NOC_NODE_ID_LOGICAL 0xffb20148ULL
#define TENSIX_X 1
#define TENSIX_Y 2
#define TLB_SIZE (1ULL << 21)  // 2MB

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
    void *mmio = MAP_FAILED;
    uint32_t tlb_id = 0;
    int tlb_allocated = 0;
    int r;
    struct tenstorrent_get_device_info dev_info;
    struct sigaction sa;
    struct tenstorrent_allocate_tlb alloc_tlb;
    uint64_t target_addr;
    uint64_t aligned_addr;
    uint64_t offset;
    struct tenstorrent_configure_tlb cfg_tlb;
    volatile uint32_t *node_id_ptr;
    int iteration;
    
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

    // Get device info to verify it's Blackhole
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

    // Allocate TLB
    memset(&alloc_tlb, 0, sizeof(alloc_tlb));
    alloc_tlb.in.size = TLB_SIZE;

    r = ioctl(fd, TENSTORRENT_IOCTL_ALLOCATE_TLB, &alloc_tlb);
    if (r != 0) {
        perror("ioctl ALLOCATE_TLB");
        close(fd);
        return 1;
    }
    tlb_id = alloc_tlb.out.id;
    tlb_allocated = 1;
    printf("Allocated TLB: id=%u, mmap_offset_uc=0x%llx\n", 
           tlb_id, (unsigned long long)alloc_tlb.out.mmap_offset_uc);

    // mmap the TLB
    mmio = mmap(NULL, TLB_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, 
                fd, alloc_tlb.out.mmap_offset_uc);
    if (mmio == MAP_FAILED) {
        perror("mmap");
        goto cleanup;
    }
    printf("Mapped TLB at %p\n", mmio);

    // Configure TLB to point at Tensix (1, 2)
    target_addr = NOC_NODE_ID_LOGICAL;
    aligned_addr = target_addr & ~(TLB_SIZE - 1);
    offset = target_addr & (TLB_SIZE - 1);

    printf("Target address: 0x%llx\n", (unsigned long long)target_addr);
    printf("Aligned address: 0x%llx\n", (unsigned long long)aligned_addr);
    printf("Offset within TLB: 0x%llx\n", (unsigned long long)offset);

    memset(&cfg_tlb, 0, sizeof(cfg_tlb));
    cfg_tlb.in.id = tlb_id;
    cfg_tlb.in.config.addr = aligned_addr;
    cfg_tlb.in.config.x_start = 0;
    cfg_tlb.in.config.y_start = 0;
    cfg_tlb.in.config.x_end = TENSIX_X;
    cfg_tlb.in.config.y_end = TENSIX_Y;
    cfg_tlb.in.config.noc = 0;
    cfg_tlb.in.config.mcast = 0;
    cfg_tlb.in.config.ordering = 0;
    cfg_tlb.in.config.linked = 0;
    cfg_tlb.in.config.static_vc = 0;

    r = ioctl(fd, TENSTORRENT_IOCTL_CONFIGURE_TLB, &cfg_tlb);
    if (r != 0) {
        perror("ioctl CONFIGURE_TLB");
        goto cleanup;
    }
    printf("Configured TLB to Tensix (%u, %u) at address 0x%llx\n",
           TENSIX_X, TENSIX_Y, (unsigned long long)aligned_addr);

    // Set up longjmp target for signal handlers
    if (sigsetjmp(signal_jmp_buf, 1) != 0) {
        // Returned from signal handler via siglongjmp
        printf("Resumed execution after signal\n");
        goto cleanup;
    }

    // Read NODE_ID through the window in a loop
    node_id_ptr = (volatile uint32_t *)((uint8_t *)mmio + offset);
    
    printf("\nStarting continuous read loop (Ctrl+C to exit)...\n\n");
    
    iteration = 0;
    while (1) {
        uint32_t node_id = *node_id_ptr;
        
        // Extract x and y coordinates from node_id
        uint32_t node_id_x = (node_id >> 0x0) & 0x3f;
        uint32_t node_id_y = (node_id >> 0x6) & 0x3f;

        printf("[%d] Read NODE_ID: 0x%08x -> (%u, %u) ",
               iteration, node_id, node_id_x, node_id_y);

        if (node_id_x == TENSIX_X && node_id_y == TENSIX_Y) {
            printf("✓ PASS\n");
        } else {
            printf("✗ FAIL (expected %u, %u)\n", TENSIX_X, TENSIX_Y);
        }

        fflush(stdout);
        iteration++;
        usleep(100000);
    }

cleanup:
    printf("\nCleaning up...\n");

    // Test munmap when mappings might be zapped
    if (mmio != MAP_FAILED) {
        printf("Calling munmap(%p, %llu)...\n", mmio, (unsigned long long)TLB_SIZE);
        r = munmap(mmio, TLB_SIZE);
        if (r != 0) {
            printf("  munmap failed: %s\n", strerror(errno));
        } else {
            printf("  munmap succeeded\n");
        }
    }

    // Test TLB free when mappings might be zapped
    if (tlb_allocated) {
        printf("Calling TENSTORRENT_IOCTL_FREE_TLB (id=%u)...\n", tlb_id);
        struct tenstorrent_free_tlb free_tlb;
        memset(&free_tlb, 0, sizeof(free_tlb));
        free_tlb.in.id = tlb_id;

        r = ioctl(fd, TENSTORRENT_IOCTL_FREE_TLB, &free_tlb);
        if (r != 0) {
            printf("  FREE_TLB failed: %s\n", strerror(errno));
        } else {
            printf("  FREE_TLB succeeded\n");
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

