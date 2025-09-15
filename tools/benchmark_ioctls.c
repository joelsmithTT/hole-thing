/**
 * SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Description:
 * A standalone, single-threaded benchmark tool to measure the latency of
 * specific ioctl calls into the Tenstorrent kernel driver. This tool is
 * self-contained and communicates directly with the driver to isolate its
 * performance from any user-space library overhead.
 *
 * Benchmarks performed:
 * 1. A null syscall (getpid) to establish a baseline.
 * 2. GET_DEVICE_INFO / GET_DRIVER_INFO for basic driver queries.
 * 3. PIN/UNPIN_PAGES for various buffer sizes (4KiB, 2MiB, 1GiB).
 * 4. ALLOCATE/FREE_TLB for a representative TLB size.
 * 5. CONFIGURE_TLB for a previously allocated TLB.
 *
 * Build command:
 * gcc -O2 -Wall benchmark_ioctls.c -o benchmark_ioctls -lrt
 *
 * Disclaimer:
 * 100% AI-generated code. Use at your own risk!
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <stdint.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/types.h>
#include <linux/mman.h> // For MAP_HUGE_* flags

// --- Standalone IOCTL Header Definitions ---
// Contents from the user-provided ioctl.h are included here directly.

#define TENSTORRENT_IOCTL_MAGIC 0xFA

#define TENSTORRENT_IOCTL_GET_DEVICE_INFO	_IO(TENSTORRENT_IOCTL_MAGIC, 0)
#define TENSTORRENT_IOCTL_GET_HARVESTING	_IO(TENSTORRENT_IOCTL_MAGIC, 1)
#define TENSTORRENT_IOCTL_QUERY_MAPPINGS	_IO(TENSTORRENT_IOCTL_MAGIC, 2)
#define TENSTORRENT_IOCTL_ALLOCATE_DMA_BUF	_IO(TENSTORRENT_IOCTL_MAGIC, 3)
#define TENSTORRENT_IOCTL_FREE_DMA_BUF		_IO(TENSTORRENT_IOCTL_MAGIC, 4)
#define TENSTORRENT_IOCTL_GET_DRIVER_INFO	_IO(TENSTORRENT_IOCTL_MAGIC, 5)
#define TENSTORRENT_IOCTL_RESET_DEVICE		_IO(TENSTORRENT_IOCTL_MAGIC, 6)
#define TENSTORRENT_IOCTL_PIN_PAGES		_IO(TENSTORRENT_IOCTL_MAGIC, 7)
#define TENSTORRENT_IOCTL_LOCK_CTL		_IO(TENSTORRENT_IOCTL_MAGIC, 8)
#define TENSTORRENT_IOCTL_MAP_PEER_BAR		_IO(TENSTORRENT_IOCTL_MAGIC, 9)
#define TENSTORRENT_IOCTL_UNPIN_PAGES		_IO(TENSTORRENT_IOCTL_MAGIC, 10)
#define TENSTORRENT_IOCTL_ALLOCATE_TLB		_IO(TENSTORRENT_IOCTL_MAGIC, 11)
#define TENSTORRENT_IOCTL_FREE_TLB		_IO(TENSTORRENT_IOCTL_MAGIC, 12)
#define TENSTORRENT_IOCTL_CONFIGURE_TLB		_IO(TENSTORRENT_IOCTL_MAGIC, 13)
#define TENSTORRENT_IOCTL_SET_NOC_CLEANUP	_IO(TENSTORRENT_IOCTL_MAGIC, 14)

struct tenstorrent_get_device_info_in { __u32 output_size_bytes; };
struct tenstorrent_get_device_info_out {
	__u32 output_size_bytes; __u16 vendor_id; __u16 device_id;
	__u16 subsystem_vendor_id; __u16 subsystem_id; __u16 bus_dev_fn;
	__u16 max_dma_buf_size_log2; __u16 pci_domain;
};
struct tenstorrent_get_device_info {
	struct tenstorrent_get_device_info_in in;
	struct tenstorrent_get_device_info_out out;
};

struct tenstorrent_get_driver_info_in { __u32 output_size_bytes; };
struct tenstorrent_get_driver_info_out {
	__u32 output_size_bytes; __u32 driver_version;
	__u8 driver_version_major; __u8 driver_version_minor;
	__u8 driver_version_patch; __u8 reserved0;
};
struct tenstorrent_get_driver_info {
	struct tenstorrent_get_driver_info_in in;
	struct tenstorrent_get_driver_info_out out;
};

struct tenstorrent_pin_pages_in {
	__u32 output_size_bytes; __u32 flags;
	__u64 virtual_address; __u64 size;
};
struct tenstorrent_pin_pages_out { __u64 physical_address; };
struct tenstorrent_pin_pages {
	struct tenstorrent_pin_pages_in in;
	struct tenstorrent_pin_pages_out out;
};

struct tenstorrent_unpin_pages_in { __u64 virtual_address; __u64 size; __u64 reserved; };
struct tenstorrent_unpin_pages_out {};
struct tenstorrent_unpin_pages {
	struct tenstorrent_unpin_pages_in in;
	struct tenstorrent_unpin_pages_out out;
};

struct tenstorrent_allocate_tlb_in { __u64 size; __u64 reserved; };
struct tenstorrent_allocate_tlb_out {
	__u32 id; __u32 reserved0;
	__u64 mmap_offset_uc; __u64 mmap_offset_wc;
	__u64 reserved1;
};
struct tenstorrent_allocate_tlb {
	struct tenstorrent_allocate_tlb_in in;
	struct tenstorrent_allocate_tlb_out out;
};

struct tenstorrent_free_tlb_in { __u32 id; };
struct tenstorrent_free_tlb_out {};
struct tenstorrent_free_tlb {
	struct tenstorrent_free_tlb_in in;
	struct tenstorrent_free_tlb_out out;
};

struct tenstorrent_noc_tlb_config {
	__u64 addr; __u16 x_end; __u16 y_end; __u16 x_start; __u16 y_start;
	__u8 noc; __u8 mcast; __u8 ordering; __u8 linked; __u8 static_vc;
	__u8 reserved0[3]; __u32 reserved1[2];
};
struct tenstorrent_configure_tlb_in { __u32 id; struct tenstorrent_noc_tlb_config config; };
struct tenstorrent_configure_tlb_out { __u64 reserved; };
struct tenstorrent_configure_tlb {
	struct tenstorrent_configure_tlb_in in;
	struct tenstorrent_configure_tlb_out out;
};

// --- End of Standalone IOCTL Header ---

#define FATAL(fmt, ...) do { \
    fprintf(stderr, "ERROR: %s:%d " fmt " (errno: %s)\n", \
            __FILE__, __LINE__, ##__VA_ARGS__, strerror(errno)); \
    exit(EXIT_FAILURE); \
} while (0)

// Number of iterations for each benchmark to get stable statistics.
#define N_ITERATIONS 1000

// Structure to hold timing statistics.
typedef struct {
    long long min_ns;
    long long max_ns;
    long long total_ns;
    long long count;
} timing_stats;

// --- Helper Functions ---

static void stats_init(timing_stats *stats) {
    stats->min_ns = -1;
    stats->max_ns = 0;
    stats->total_ns = 0;
    stats->count = 0;
}

static void stats_update(timing_stats *stats, long long duration_ns) {
    if (stats->min_ns == -1 || duration_ns < stats->min_ns) {
        stats->min_ns = duration_ns;
    }
    if (duration_ns > stats->max_ns) {
        stats->max_ns = duration_ns;
    }
    stats->total_ns += duration_ns;
    stats->count++;
}

static void stats_print(const char *name, timing_stats *stats) {
    if (stats->count == 0) {
        printf("%-35s: No data\n", name);
        return;
    }
    double avg_us = (double)stats->total_ns / stats->count / 1000.0;
    double min_us = (double)stats->min_ns / 1000.0;
    double max_us = (double)stats->max_ns / 1000.0;
    printf("%-35s: avg=%9.2f us | min=%9.2f us | max=%9.2f us\n", name, avg_us, min_us, max_us);
}

static const char* size_to_str(size_t size) {
    static char buf[32];
    if (size == (1UL << 30)) snprintf(buf, sizeof(buf), "1GiB");
    else if (size == (2UL << 20)) snprintf(buf, sizeof(buf), "2MiB");
    else if (size == (4UL << 10)) snprintf(buf, sizeof(buf), "4KiB");
    else snprintf(buf, sizeof(buf), "%zuB", size);
    return buf;
}

static void* allocate_buffer(size_t size) {
    void* addr = MAP_FAILED;
    if (size >= (1UL << 30) && (size % (1UL << 30)) == 0) {
        addr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB | MAP_HUGE_1GB, -1, 0);
    }
    if (addr == MAP_FAILED && size >= (1UL << 21) && (size % (1UL << 21)) == 0) {
        addr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB | MAP_HUGE_2MB, -1, 0);
    }
    if (addr == MAP_FAILED) {
        if (posix_memalign(&addr, sysconf(_SC_PAGESIZE), size) != 0) FATAL("posix_memalign failed");
    }
    return addr;
}

// --- Benchmark Functions ---

void benchmark_null_syscall(void) {
    printf("--- Benchmarking Baseline Syscall Latency ---\n");
    timing_stats stats;
    stats_init(&stats);
    struct timespec start, end;

    for (int i = 0; i < N_ITERATIONS * 10; ++i) { // Run more iterations for higher precision
        clock_gettime(CLOCK_MONOTONIC, &start);
        getpid();
        clock_gettime(CLOCK_MONOTONIC, &end);
        stats_update(&stats, (end.tv_sec - start.tv_sec) * 1000000000LL + (end.tv_nsec - start.tv_nsec));
    }
    stats_print("getpid()", &stats);
}

void benchmark_info_calls(int fd) {
    printf("--- Benchmarking Informational IOCTLs ---\n");
    timing_stats dev_info_stats, drv_info_stats;
    stats_init(&dev_info_stats);
    stats_init(&drv_info_stats);
    struct timespec start, end;

    for (int i = 0; i < N_ITERATIONS; ++i) {
        // GET_DEVICE_INFO
        struct tenstorrent_get_device_info dev_info_cmd = {0};
        dev_info_cmd.in.output_size_bytes = sizeof(dev_info_cmd.out);
        clock_gettime(CLOCK_MONOTONIC, &start);
        if (ioctl(fd, TENSTORRENT_IOCTL_GET_DEVICE_INFO, &dev_info_cmd) != 0) FATAL("ioctl(GET_DEVICE_INFO) failed");
        clock_gettime(CLOCK_MONOTONIC, &end);
        stats_update(&dev_info_stats, (end.tv_sec - start.tv_sec) * 1000000000LL + (end.tv_nsec - start.tv_nsec));

        // GET_DRIVER_INFO
        struct tenstorrent_get_driver_info drv_info_cmd = {0};
        drv_info_cmd.in.output_size_bytes = sizeof(drv_info_cmd.out);
        clock_gettime(CLOCK_MONOTONIC, &start);
        if (ioctl(fd, TENSTORRENT_IOCTL_GET_DRIVER_INFO, &drv_info_cmd) != 0) FATAL("ioctl(GET_DRIVER_INFO) failed");
        clock_gettime(CLOCK_MONOTONIC, &end);
        stats_update(&drv_info_stats, (end.tv_sec - start.tv_sec) * 1000000000LL + (end.tv_nsec - start.tv_nsec));
    }
    stats_print("GET_DEVICE_INFO", &dev_info_stats);
    stats_print("GET_DRIVER_INFO", &drv_info_stats);
}

void benchmark_pin_unpin_pages(int fd) {
    printf("--- Benchmarking PIN_PAGES / UNPIN_PAGES ---\n");
    size_t sizes[] = { 4 * 1024, 2 * 1024 * 1024, 1UL * 1024 * 1024 * 1024 };

    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {
        size_t size = sizes[i];
        char bench_name[64];
        timing_stats pin_stats, unpin_stats;
        stats_init(&pin_stats);
        stats_init(&unpin_stats);

        void* buf = allocate_buffer(size);
        if (!buf) FATAL("Failed to allocate buffer of size %zu", size);

        for (int j = 0; j < N_ITERATIONS; ++j) {
            struct timespec start, end;
            struct tenstorrent_pin_pages pin_pages_cmd = {0};
            pin_pages_cmd.in.output_size_bytes = sizeof(pin_pages_cmd.out);
            pin_pages_cmd.in.virtual_address = (uint64_t)buf;
            pin_pages_cmd.in.size = size;

            clock_gettime(CLOCK_MONOTONIC, &start);
            if (ioctl(fd, TENSTORRENT_IOCTL_PIN_PAGES, &pin_pages_cmd) != 0) FATAL("ioctl(PIN_PAGES) failed");
            clock_gettime(CLOCK_MONOTONIC, &end);
            stats_update(&pin_stats, (end.tv_sec - start.tv_sec) * 1000000000LL + (end.tv_nsec - start.tv_nsec));

            struct tenstorrent_unpin_pages unpin_pages_cmd = {0};
            unpin_pages_cmd.in.virtual_address = (uint64_t)buf;
            unpin_pages_cmd.in.size = size;

            clock_gettime(CLOCK_MONOTONIC, &start);
            if (ioctl(fd, TENSTORRENT_IOCTL_UNPIN_PAGES, &unpin_pages_cmd) != 0) FATAL("ioctl(UNPIN_PAGES) failed");
            clock_gettime(CLOCK_MONOTONIC, &end);
            stats_update(&unpin_stats, (end.tv_sec - start.tv_sec) * 1000000000LL + (end.tv_nsec - start.tv_nsec));
        }

        snprintf(bench_name, sizeof(bench_name), "PIN_PAGES (%s)", size_to_str(size));
        stats_print(bench_name, &pin_stats);
        snprintf(bench_name, sizeof(bench_name), "UNPIN_PAGES (%s)", size_to_str(size));
        stats_print(bench_name, &unpin_stats);

        munmap(buf, size);
    }
}

void benchmark_tlb_management(int fd) {
    printf("--- Benchmarking TLB Management ---\n");
    timing_stats alloc_stats, free_stats, config_stats;
    stats_init(&alloc_stats);
    stats_init(&free_stats);
    stats_init(&config_stats);

    for (int i = 0; i < N_ITERATIONS; i++) {
        struct timespec start, end;

        struct tenstorrent_allocate_tlb alloc_cmd = {0};
        alloc_cmd.in.size = 2 * 1024 * 1024; // Use 2MiB as a representative size
        clock_gettime(CLOCK_MONOTONIC, &start);
        if (ioctl(fd, TENSTORRENT_IOCTL_ALLOCATE_TLB, &alloc_cmd) != 0) FATAL("ioctl(ALLOCATE_TLB) failed");
        clock_gettime(CLOCK_MONOTONIC, &end);
        stats_update(&alloc_stats, (end.tv_sec - start.tv_sec) * 1000000000LL + (end.tv_nsec - start.tv_nsec));

        uint32_t tlb_id = alloc_cmd.out.id;

        struct tenstorrent_configure_tlb config_cmd = {0};
        config_cmd.in.id = tlb_id;
        config_cmd.in.config.addr = 0;
        config_cmd.in.config.x_end = 0;
        config_cmd.in.config.y_end = 0;
        clock_gettime(CLOCK_MONOTONIC, &start);
        if (ioctl(fd, TENSTORRENT_IOCTL_CONFIGURE_TLB, &config_cmd) != 0) FATAL("ioctl(CONFIGURE_TLB) failed");
        clock_gettime(CLOCK_MONOTONIC, &end);
        stats_update(&config_stats, (end.tv_sec - start.tv_sec) * 1000000000LL + (end.tv_nsec - start.tv_nsec));

        struct tenstorrent_free_tlb free_cmd = {0};
        free_cmd.in.id = tlb_id;
        clock_gettime(CLOCK_MONOTONIC, &start);
        if (ioctl(fd, TENSTORRENT_IOCTL_FREE_TLB, &free_cmd) != 0) FATAL("ioctl(FREE_TLB) failed");
        clock_gettime(CLOCK_MONOTONIC, &end);
        stats_update(&free_stats, (end.tv_sec - start.tv_sec) * 1000000000LL + (end.tv_nsec - start.tv_nsec));
    }
    stats_print("ALLOCATE_TLB (2MiB)", &alloc_stats);
    stats_print("CONFIGURE_TLB", &config_stats);
    stats_print("FREE_TLB (2MiB)", &free_stats);
}

// --- Main Function ---

int main(void) {
    int fd = -1;
    char dev_path[32];

    benchmark_null_syscall();

    for (int i = 0; i < 8; ++i) {
        snprintf(dev_path, sizeof(dev_path), "/dev/tenstorrent/%d", i);
        fd = open(dev_path, O_RDWR);
        if (fd >= 0) {
            printf("\nOpened device: %s\n\n", dev_path);
            break;
        }
    }

    if (fd < 0) {
        fprintf(stderr, "Warning: Could not open any /dev/tenstorrent device. Skipping driver benchmarks.\n");
        return 0;
    }

    benchmark_info_calls(fd);
    printf("\n");
    benchmark_pin_unpin_pages(fd);
    printf("\n");
    benchmark_tlb_management(fd);

    printf("\nBenchmark complete.\n");
    close(fd);
    return 0;
}
