// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
// SPDX-License-Identifier: GPL-2.0-only
//
// DRAM Benchmark - TLB-mapped throughput test
//
// Measures DRAM read/write throughput scaling with thread count.
// Compares shared TLB window vs private TLB windows per thread.

#include "holething.hpp"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>
#include <pthread.h>

using namespace tt;

static constexpr size_t TLB_SIZE_2M = TT_TLB_SIZE_2M;  // 2 MiB windows
static constexpr size_t TLB_SIZE_4G = TT_TLB_SIZE_4G;  // 4 GiB windows (BH only)

// Simple barrier using pthread
class Barrier {
    pthread_barrier_t barrier;
public:
    explicit Barrier(unsigned count) {
        pthread_barrier_init(&barrier, nullptr, count);
    }
    ~Barrier() {
        pthread_barrier_destroy(&barrier);
    }
    void wait() {
        pthread_barrier_wait(&barrier);
    }
};

struct Config {
    const char* device_path = nullptr;
    int num_threads = 1;
    size_t total_size_mib = 64;
    int iterations = 3;
    bool read_mode = false;
    bool shared_mode = false;  // false = private
    bool use_4g_tlb = false;   // use 4 GiB TLB windows (BH only)
    bool multi_channel = false; // spread threads across GDDR channels (BH only)
    uint16_t noc_x = 0;
    uint16_t noc_y = 0;
    bool coords_specified = false;
};

// Blackhole GDDR channel coordinates (8 channels)
static const std::pair<uint16_t, uint16_t> BH_GDDR_COORDS[] = {
    {17, 12}, {18, 12}, {17, 15}, {18, 15},
    {17, 18}, {18, 18}, {17, 21}, {18, 21}
};
static constexpr int NUM_BH_GDDR_CHANNELS = 8;

struct ThreadResult {
    double elapsed_ms;
};

// Worker for private mode: each thread has its own TLB window
static void worker_private(
    Device* device,
    int thread_id,
    int num_threads,
    uint16_t noc_x,
    uint16_t noc_y,
    size_t total_size,
    bool read_mode,
    size_t tlb_size,
    bool multi_channel,
    Barrier* start_barrier,
    Barrier* end_barrier,
    ThreadResult* result)
{
    size_t per_thread = total_size / num_threads;
    
    // In multi-channel mode: each thread writes to address 0 on its own channel
    // In single-channel mode: each thread writes to a different offset
    uint64_t my_start = multi_channel ? 0 : (uint64_t)thread_id * per_thread;
    size_t remaining = per_thread;

    // Allocate our private TLB window
    TlbWindow tlb(*device, tlb_size, TT_MMIO_CACHE_MODE_WC);
    void* mmio = tlb.get_mmio();

    // For 4G TLB: map to 0x0 (must be 4G-aligned), write at offset within window
    // For 2M TLB: remap in the loop
    bool needs_remap = (tlb_size == TLB_SIZE_2M);
    
    if (!needs_remap) {
        // 4G TLB: map once to address 0 (4G-aligned)
        tlb.map(noc_x, noc_y, 0);
    }

    // Use a fixed chunk size for memcpy (2 MiB chunks even with 4G TLB)
    // This avoids allocating a huge buffer
    static constexpr size_t CHUNK_SIZE = 2 * 1024 * 1024;  // 2 MiB
    std::vector<uint8_t> buffer(CHUNK_SIZE);
    memset(buffer.data(), 0xAA + thread_id, buffer.size());

    // Wait for all threads to be ready
    start_barrier->wait();

    auto t_start = std::chrono::steady_clock::now();

    uint64_t addr = my_start;
    // For 4G TLB: offset starts at my_start (within the 4G window)
    // For 2M TLB: offset resets to 0 after each remap
    size_t offset_in_window = needs_remap ? 0 : my_start;
    
    while (remaining > 0) {
        size_t chunk = std::min(remaining, CHUNK_SIZE);

        if (needs_remap) {
            // 2M TLB: remap for each chunk
            tlb.map(noc_x, noc_y, addr);
            offset_in_window = 0;
        }

        uint8_t* target = static_cast<uint8_t*>(mmio) + offset_in_window;
        
        if (read_mode) {
            memcpy(buffer.data(), target, chunk);
        } else {
            memcpy(target, buffer.data(), chunk);
        }

        addr += chunk;
        offset_in_window += chunk;
        remaining -= chunk;
    }

    auto t_end = std::chrono::steady_clock::now();
    result->elapsed_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();

    end_barrier->wait();
}

// Worker for shared mode: all threads share one TLB window
static void worker_shared(
    TlbWindow* tlb,
    int thread_id,
    int num_threads,
    uint16_t noc_x,
    uint16_t noc_y,
    size_t total_size,
    bool read_mode,
    size_t tlb_size,
    Barrier* start_barrier,
    Barrier* chunk_barrier,
    Barrier* end_barrier,
    ThreadResult* result)
{
    void* mmio = tlb->get_mmio();
    bool needs_remap = (tlb_size == TLB_SIZE_2M);
    
    // For 2M TLB: divide each window among threads, remap between windows
    // For 4G TLB: divide total work among threads, no remapping needed
    
    static constexpr size_t CHUNK_SIZE = 2 * 1024 * 1024;  // 2 MiB memcpy chunks
    
    size_t per_thread_total;
    size_t my_offset_base;
    size_t num_windows;
    size_t slice_per_window;
    
    if (needs_remap) {
        // 2M mode: each window divided among threads
        slice_per_window = TLB_SIZE_2M / num_threads;
        my_offset_base = thread_id * slice_per_window;
        num_windows = total_size / TLB_SIZE_2M;
        per_thread_total = slice_per_window * num_windows;
    } else {
        // 4G mode: total work divided among threads, no remapping
        per_thread_total = total_size / num_threads;
        my_offset_base = thread_id * per_thread_total;
        num_windows = 1;  // Single "window" conceptually
        slice_per_window = per_thread_total;
    }

    // Allocate buffer (use smaller chunks for memcpy)
    size_t buffer_size = std::min(slice_per_window, CHUNK_SIZE);
    std::vector<uint8_t> buffer(buffer_size);
    memset(buffer.data(), 0xBB + thread_id, buffer.size());

    start_barrier->wait();

    auto t_start = std::chrono::steady_clock::now();

    if (needs_remap) {
        // 2M TLB: remap for each window position
        for (size_t win = 0; win < num_windows; win++) {
            uint64_t window_addr = win * TLB_SIZE_2M;

            // Thread 0 remaps the window, others wait
            if (thread_id == 0) {
                tlb->map(noc_x, noc_y, window_addr);
            }
            chunk_barrier->wait();  // Ensure window is mapped before access

            // Each thread accesses its slice
            uint8_t* my_ptr = static_cast<uint8_t*>(mmio) + my_offset_base;
            if (read_mode) {
                memcpy(buffer.data(), my_ptr, slice_per_window);
            } else {
                memcpy(my_ptr, buffer.data(), slice_per_window);
            }

            chunk_barrier->wait();  // Ensure all done before next remap
        }
    } else {
        // 4G TLB: no remapping, just write our slice in chunks
        size_t remaining = per_thread_total;
        size_t offset = my_offset_base;
        
        while (remaining > 0) {
            size_t chunk = std::min(remaining, buffer_size);
            uint8_t* ptr = static_cast<uint8_t*>(mmio) + offset;
            
            if (read_mode) {
                memcpy(buffer.data(), ptr, chunk);
            } else {
                memcpy(ptr, buffer.data(), chunk);
            }
            
            offset += chunk;
            remaining -= chunk;
        }
    }

    auto t_end = std::chrono::steady_clock::now();
    result->elapsed_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();

    end_barrier->wait();
}

static void print_usage(const char* prog) {
    fprintf(stderr, R"(DRAM Benchmark - TLB-mapped throughput test

Usage: %s [OPTIONS] <device>

Arguments:
  <device>              Device path (e.g., /dev/tenstorrent/0)

Options:
  -t, --threads <N>     Number of threads [default: 1]
  -s, --size <MiB>      Total transfer size in MiB [default: 64]
  -n, --iterations <N>  Repeat benchmark N times [default: 3]
  -r, --read            Read from DRAM (default is write)
  --shared              All threads share one TLB window
  --private             Each thread gets its own TLB window [default]
  --tlb-4g              Use 4 GiB TLB windows (Blackhole only)
  --multi-channel       Spread threads across GDDR channels (Blackhole only, implies --tlb-4g)
  -x <X>                NOC X coordinate (not needed with --multi-channel)
  -y <Y>                NOC Y coordinate (not needed with --multi-channel)
  -h, --help            Print this help

TLB Size:
  Default uses 2 MiB TLB windows, requiring remapping every 2 MiB.
  --tlb-4g uses 4 GiB windows (Blackhole only), mapping once at start.

Multi-Channel Mode (Blackhole):
  --multi-channel assigns each thread to a different GDDR channel:
    Thread 0 -> (17,12), Thread 1 -> (18,12), Thread 2 -> (17,15), ...
  This tests aggregate DRAM bandwidth across channels.
  Implies --tlb-4g. Max 8 threads (one per channel).

Examples:
  # Single channel baseline
  %s -t 1 -s 512 -x 17 -y 12 --tlb-4g /dev/tenstorrent/0

  # Multi-channel scaling test (each thread hits different GDDR)
  for t in 1 2 4 8; do
    echo "=== $t channels ==="
    %s -t $t -s 512 --multi-channel /dev/tenstorrent/0
  done
)", prog, prog, prog);
}

static bool parse_args(int argc, char** argv, Config& cfg) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            exit(0);
        } else if (strcmp(argv[i], "-t") == 0 || strcmp(argv[i], "--threads") == 0) {
            if (++i >= argc) { fprintf(stderr, "Missing argument for -t\n"); return false; }
            cfg.num_threads = atoi(argv[i]);
        } else if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--size") == 0) {
            if (++i >= argc) { fprintf(stderr, "Missing argument for -s\n"); return false; }
            cfg.total_size_mib = atoi(argv[i]);
        } else if (strcmp(argv[i], "-n") == 0 || strcmp(argv[i], "--iterations") == 0) {
            if (++i >= argc) { fprintf(stderr, "Missing argument for -n\n"); return false; }
            cfg.iterations = atoi(argv[i]);
        } else if (strcmp(argv[i], "-r") == 0 || strcmp(argv[i], "--read") == 0) {
            cfg.read_mode = true;
        } else if (strcmp(argv[i], "--shared") == 0) {
            cfg.shared_mode = true;
        } else if (strcmp(argv[i], "--private") == 0) {
            cfg.shared_mode = false;
        } else if (strcmp(argv[i], "--tlb-4g") == 0) {
            cfg.use_4g_tlb = true;
        } else if (strcmp(argv[i], "--multi-channel") == 0) {
            cfg.multi_channel = true;
            cfg.use_4g_tlb = true;  // multi-channel implies 4G TLB
        } else if (strcmp(argv[i], "-x") == 0) {
            if (++i >= argc) { fprintf(stderr, "Missing argument for -x\n"); return false; }
            cfg.noc_x = atoi(argv[i]);
            cfg.coords_specified = true;
        } else if (strcmp(argv[i], "-y") == 0) {
            if (++i >= argc) { fprintf(stderr, "Missing argument for -y\n"); return false; }
            cfg.noc_y = atoi(argv[i]);
            cfg.coords_specified = true;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            return false;
        } else {
            cfg.device_path = argv[i];
        }
    }

    if (!cfg.device_path) {
        fprintf(stderr, "Error: Missing device path\n");
        return false;
    }
    if (!cfg.multi_channel && !cfg.coords_specified) {
        fprintf(stderr, "Error: Must specify -x and -y coordinates (or use --multi-channel)\n");
        return false;
    }
    if (cfg.multi_channel && cfg.num_threads > NUM_BH_GDDR_CHANNELS) {
        fprintf(stderr, "Error: --multi-channel supports max %d threads (one per GDDR channel)\n",
                NUM_BH_GDDR_CHANNELS);
        return false;
    }
    if (cfg.multi_channel && cfg.shared_mode) {
        fprintf(stderr, "Error: --multi-channel is not compatible with --shared mode\n");
        return false;
    }
    if (cfg.num_threads < 1) {
        fprintf(stderr, "Error: Thread count must be >= 1\n");
        return false;
    }
    if (cfg.iterations < 1) {
        fprintf(stderr, "Error: Iteration count must be >= 1\n");
        return false;
    }

    return true;
}

int main(int argc, char** argv) {
    Config cfg;
    if (!parse_args(argc, argv, cfg)) {
        fprintf(stderr, "\nRun with --help for usage.\n");
        return 1;
    }

    size_t total_size = cfg.total_size_mib * 1024 * 1024;

    try {
        Device device(cfg.device_path);
        
        // Validate 4G TLB is only used on Blackhole
        if (cfg.use_4g_tlb && !device.is_blackhole()) {
            fprintf(stderr, "Error: --tlb-4g is only supported on Blackhole devices\n");
            return 1;
        }
        
        size_t tlb_size = cfg.use_4g_tlb ? TLB_SIZE_4G : TLB_SIZE_2M;
        size_t tlb_size_mib = tlb_size / (1024 * 1024);

        // Validate size constraints
        if (!cfg.use_4g_tlb) {
            // 2M TLB mode: size must be multiple of 2 MiB
            if (total_size % TLB_SIZE_2M != 0) {
                fprintf(stderr, "Error: Total size must be a multiple of 2 MiB\n");
                return 1;
            }
        }

        if (cfg.shared_mode) {
            if (!cfg.use_4g_tlb && TLB_SIZE_2M % cfg.num_threads != 0) {
                fprintf(stderr, "Error: In shared mode with 2M TLB, 2 MiB must be evenly divisible by thread count\n");
                fprintf(stderr, "       Valid thread counts: 1, 2, 4, 8, 16, ...\n");
                return 1;
            }
        } else {
            // Private mode: each thread needs enough work
            size_t per_thread = total_size / cfg.num_threads;
            if (!cfg.use_4g_tlb) {
                if (per_thread < TLB_SIZE_2M) {
                    fprintf(stderr, "Error: In private mode with 2M TLB, each thread needs at least 2 MiB\n");
                    fprintf(stderr, "       With %d threads, total size must be >= %d MiB\n",
                            cfg.num_threads, cfg.num_threads * 2);
                    return 1;
                }
                if (per_thread % TLB_SIZE_2M != 0) {
                    fprintf(stderr, "Error: Per-thread size must be a multiple of 2 MiB\n");
                    fprintf(stderr, "       total_size / num_threads = %zu MiB, not aligned\n",
                            per_thread / (1024 * 1024));
                    return 1;
                }
            }
        }

        // Validate multi-channel is only used on Blackhole
        if (cfg.multi_channel && !device.is_blackhole()) {
            fprintf(stderr, "Error: --multi-channel is only supported on Blackhole devices\n");
            return 1;
        }

        printf("DRAM Benchmark\n");
        printf("==============\n");
        printf("Device: %s (%s)\n", cfg.device_path,
               device.is_blackhole() ? "Blackhole" :
               device.is_wormhole() ? "Wormhole" : "Unknown");
        
        if (cfg.multi_channel) {
            printf("Target: %d GDDR channel%s: ", cfg.num_threads, cfg.num_threads > 1 ? "s" : "");
            for (int i = 0; i < cfg.num_threads; i++) {
                printf("(%u,%u)%s", BH_GDDR_COORDS[i].first, BH_GDDR_COORDS[i].second,
                       i < cfg.num_threads - 1 ? ", " : "\n");
            }
        } else {
            printf("Target: NOC (%u, %u) @ address 0x0\n", cfg.noc_x, cfg.noc_y);
        }
        printf("TLB: %zu %s windows%s\n", 
               cfg.use_4g_tlb ? (size_t)(tlb_size / (1024ULL * 1024 * 1024)) : tlb_size_mib,
               cfg.use_4g_tlb ? "GiB" : "MiB",
               cfg.use_4g_tlb ? " (map once, no ioctl in hot path)" : "");
        printf("Mode: %s, %d thread%s, %zu MiB %s, %d iteration%s\n",
               cfg.shared_mode ? "shared" : "private",
               cfg.num_threads, cfg.num_threads == 1 ? "" : "s",
               cfg.total_size_mib,
               cfg.read_mode ? "read" : "write",
               cfg.iterations, cfg.iterations == 1 ? "" : "s");

        size_t per_thread = total_size / cfg.num_threads;
        if (cfg.use_4g_tlb) {
            printf("       Each thread writes %zu MiB (no remaps)\n",
                   per_thread / (1024 * 1024));
        } else if (cfg.shared_mode) {
            printf("       Each thread writes %zu KiB per window, %zu window remaps\n",
                   (TLB_SIZE_2M / cfg.num_threads) / 1024,
                   total_size / TLB_SIZE_2M);
        } else {
            printf("       Each thread writes %zu MiB, %zu window remap%s per thread\n",
                   per_thread / (1024 * 1024),
                   per_thread / TLB_SIZE_2M,
                   (per_thread / TLB_SIZE_2M) == 1 ? "" : "s");
        }
        printf("\n");

        std::vector<double> throughputs;

        for (int iter = 0; iter < cfg.iterations; iter++) {
            std::vector<std::thread> threads;
            std::vector<ThreadResult> results(cfg.num_threads);

            double elapsed_ms;

            if (cfg.shared_mode) {
                // Shared mode: one TLB window, all threads share it
                TlbWindow shared_tlb(device, tlb_size, TT_MMIO_CACHE_MODE_WC);
                
                // For 4G TLB in shared mode, map once before starting
                if (cfg.use_4g_tlb) {
                    shared_tlb.map(cfg.noc_x, cfg.noc_y, 0);
                }
                
                Barrier start_barrier(cfg.num_threads);
                Barrier chunk_barrier(cfg.num_threads);
                Barrier end_barrier(cfg.num_threads);

                for (int t = 0; t < cfg.num_threads; t++) {
                    threads.emplace_back(worker_shared,
                        &shared_tlb,
                        t, cfg.num_threads,
                        cfg.noc_x, cfg.noc_y,
                        total_size,
                        cfg.read_mode,
                        tlb_size,
                        &start_barrier,
                        &chunk_barrier,
                        &end_barrier,
                        &results[t]);
                }

                for (auto& th : threads) {
                    th.join();
                }

                // In shared mode, all threads should have ~same elapsed time
                elapsed_ms = results[0].elapsed_ms;

            } else {
                // Private mode: each thread gets its own TLB
                Barrier start_barrier(cfg.num_threads);
                Barrier end_barrier(cfg.num_threads);

                for (int t = 0; t < cfg.num_threads; t++) {
                    // In multi-channel mode, each thread targets a different GDDR channel
                    uint16_t thread_noc_x = cfg.multi_channel ? BH_GDDR_COORDS[t].first : cfg.noc_x;
                    uint16_t thread_noc_y = cfg.multi_channel ? BH_GDDR_COORDS[t].second : cfg.noc_y;
                    
                    threads.emplace_back(worker_private,
                        &device,
                        t, cfg.num_threads,
                        thread_noc_x, thread_noc_y,
                        total_size,
                        cfg.read_mode,
                        tlb_size,
                        cfg.multi_channel,
                        &start_barrier,
                        &end_barrier,
                        &results[t]);
                }

                for (auto& th : threads) {
                    th.join();
                }

                // In private mode, use the max time (wait for slowest thread)
                elapsed_ms = 0;
                for (const auto& r : results) {
                    elapsed_ms = std::max(elapsed_ms, r.elapsed_ms);
                }
            }

            double throughput_mibs = static_cast<double>(cfg.total_size_mib) / (elapsed_ms / 1000.0);
            throughputs.push_back(throughput_mibs);

            printf("  Iter %d: %8.2f ms  %10.2f MiB/s\n", iter + 1, elapsed_ms, throughput_mibs);
        }

        // Calculate mean
        double sum = 0;
        for (double t : throughputs) sum += t;
        double mean = sum / throughputs.size();

        printf("----------------------------------------\n");
        printf("  Mean:             %10.2f MiB/s\n", mean);

    } catch (const std::exception& e) {
        fprintf(stderr, "Error: %s\n", e.what());
        return 1;
    }

    return 0;
}
