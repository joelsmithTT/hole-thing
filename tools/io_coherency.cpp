// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
// SPDX-License-Identifier: GPL-2.0
//
// WHAT THIS IS:
// =============
// A standalone C++ diagnostic tool for testing the DMA functionality of
// Tenstorrent devices through the tt-kmd kernel driver.
//
// This program is specifically designed to highlight and test for CPU/DMA
// cache coherency issues on the host system. It does this by running the
// same DMA test logic against two different types of host memory:
//
// 1. Driver-Allocated Buffer: This test uses the TENSTORRENT_IOCTL_ALLOCATE_DMA_BUF
//    ioctl. The underlying kernel driver uses the dma_alloc_coherent() API. On
//    platforms that are not I/O coherent (e.g. SiFive P550 Premier), this
//    function returns memory that is mapped as uncached for the CPU. This
//    bypasses the CPU cache, so both the CPU and the device see the same view
//    of memory, and this test is expected to PASS.
//
// 2. User-Pinned Buffer: This test allocates standard, cacheable memory from
//    user space using mmap() and then "pins" it for DMA using the
//    TENSTORRENT_IOCTL_PIN_PAGES ioctl. Because this memory is cached by the
//    CPU, this test will FAIL on non-coherent platforms.
//
// HOW TO BUILD:
// =============
// g++ -O2 -Wall io_coherency.cpp -o io_coherency
//
// HOW TO RUN:
// ===========
// ./io_coherency /dev/tenstorrent/0 1024
//   - Arg 1: Path to the Tenstorrent device node.
//   - Arg 2: Number of pages (size of buffer).
//

#include <iostream>
#include <vector>
#include <random>
#include <cstring>
#include <cerrno>
#include <stdexcept>
#include <algorithm>
#include <memory>
#include <unordered_set>

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

// The ioctl.h content is included directly to make the file standalone.
#ifndef TTDRIVER_IOCTL_H_INCLUDED
#define TTDRIVER_IOCTL_H_INCLUDED

#include <linux/types.h>
#include <linux/ioctl.h>

#define TENSTORRENT_DRIVER_VERSION 2

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
#define TENSTORRENT_IOCTL_SET_NOC_CLEANUP		_IO(TENSTORRENT_IOCTL_MAGIC, 14)

// For tenstorrent_mapping.mapping_id. These are not array indices.
#define TENSTORRENT_MAPPING_UNUSED		0
#define TENSTORRENT_MAPPING_RESOURCE0_UC	1
#define TENSTORRENT_MAPPING_RESOURCE0_WC	2
#define TENSTORRENT_MAPPING_RESOURCE1_UC	3
#define TENSTORRENT_MAPPING_RESOURCE1_WC	4
#define TENSTORRENT_MAPPING_RESOURCE2_UC	5
#define TENSTORRENT_MAPPING_RESOURCE2_WC	6

#define TENSTORRENT_MAX_DMA_BUFS	256
#define TENSTORRENT_MAX_INBOUND_TLBS	256

#define TENSTORRENT_RESOURCE_LOCK_COUNT 64

struct tenstorrent_get_device_info_in {
	__u32 output_size_bytes;
};

struct tenstorrent_get_device_info_out {
	__u32 output_size_bytes;
	__u16 vendor_id;
	__u16 device_id;
	__u16 subsystem_vendor_id;
	__u16 subsystem_id;
	__u16 bus_dev_fn;	// [0:2] function, [3:7] device, [8:15] bus
	__u16 max_dma_buf_size_log2;	// Since 1.0
	__u16 pci_domain;		// Since 1.23
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

// tenstorrent_allocate_dma_buf_in.flags
#define TENSTORRENT_ALLOCATE_DMA_BUF_NOC_DMA 2

struct tenstorrent_allocate_dma_buf_in {
	__u32 requested_size;
	__u8  buf_index;	// [0,TENSTORRENT_MAX_DMA_BUFS)
	__u8  flags;
	__u8  reserved0[2];
	__u64 reserved1[2];
};

struct tenstorrent_allocate_dma_buf_out {
	__u64 physical_address;	// or IOVA
	__u64 mapping_offset;
	__u32 size;
	__u32 reserved0;
	__u64 noc_address;	// valid if TENSTORRENT_ALLOCATE_DMA_BUF_NOC_DMA is set
	__u64 reserved1;
};

struct tenstorrent_allocate_dma_buf {
	struct tenstorrent_allocate_dma_buf_in in;
	struct tenstorrent_allocate_dma_buf_out out;
};

struct tenstorrent_free_dma_buf_in {
};

struct tenstorrent_free_dma_buf_out {
};

struct tenstorrent_free_dma_buf {
	struct tenstorrent_free_dma_buf_in in;
	struct tenstorrent_free_dma_buf_out out;
};

struct tenstorrent_get_driver_info_in {
	__u32 output_size_bytes;
};

struct tenstorrent_get_driver_info_out {
	__u32 output_size_bytes;
	__u32 driver_version;		// IOCTL API version
	__u8 driver_version_major;
	__u8 driver_version_minor;
	__u8 driver_version_patch;
	__u8 reserved0;
};

struct tenstorrent_get_driver_info {
	struct tenstorrent_get_driver_info_in in;
	struct tenstorrent_get_driver_info_out out;
};

// tenstorrent_reset_device_in.flags
#define TENSTORRENT_RESET_DEVICE_RESTORE_STATE 0
#define TENSTORRENT_RESET_DEVICE_RESET_PCIE_LINK 1
#define TENSTORRENT_RESET_DEVICE_CONFIG_WRITE 2

struct tenstorrent_reset_device_in {
	__u32 output_size_bytes;
	__u32 flags;
};

struct tenstorrent_reset_device_out {
	__u32 output_size_bytes;
	__u32 result;
};

struct tenstorrent_reset_device {
	struct tenstorrent_reset_device_in in;
	struct tenstorrent_reset_device_out out;
};

// tenstorrent_pin_pages_in.flags
#define TENSTORRENT_PIN_PAGES_CONTIGUOUS 1	// app attests that the pages are physically contiguous
#define TENSTORRENT_PIN_PAGES_NOC_DMA 2		// app wants to use the pages for NOC DMA
#define TENSTORRENT_PIN_PAGES_NOC_TOP_DOWN 4	// NOC DMA will be allocated top-down (default is bottom-up)

struct tenstorrent_pin_pages_in {
	__u32 output_size_bytes;
	__u32 flags;
	__u64 virtual_address;
	__u64 size;
};

struct tenstorrent_pin_pages_out {
	__u64 physical_address;	// or IOVA
};

struct tenstorrent_pin_pages_out_extended {
	__u64 physical_address;	// or IOVA
	__u64 noc_address;
};

// unpinning subset of a pinned buffer is not supported
struct tenstorrent_unpin_pages_in {
	__u64 virtual_address;	// original VA used to pin, not current VA if remapped
	__u64 size;
	__u64 reserved;
};

struct tenstorrent_unpin_pages_out {
};

struct tenstorrent_unpin_pages {
	struct tenstorrent_unpin_pages_in in;
	struct tenstorrent_unpin_pages_out out;
};

struct tenstorrent_pin_pages {
	struct tenstorrent_pin_pages_in in;
	struct tenstorrent_pin_pages_out out;
};

// tenstorrent_lock_ctl_in.flags
#define TENSTORRENT_LOCK_CTL_ACQUIRE 0
#define TENSTORRENT_LOCK_CTL_RELEASE 1
#define TENSTORRENT_LOCK_CTL_TEST 2

struct tenstorrent_lock_ctl_in {
	__u32 output_size_bytes;
	__u32 flags;
	__u8  index;
};

struct tenstorrent_lock_ctl_out {
	__u8 value;
};

struct tenstorrent_lock_ctl {
	struct tenstorrent_lock_ctl_in in;
	struct tenstorrent_lock_ctl_out out;
};

struct tenstorrent_map_peer_bar_in {
	__u32 peer_fd;
	__u32 peer_bar_index;
	__u32 peer_bar_offset;
	__u32 peer_bar_length;
	__u32 flags;
};

struct tenstorrent_map_peer_bar_out {
	__u64 dma_address;
	__u64 reserved;
};

struct tenstorrent_map_peer_bar {
	struct tenstorrent_map_peer_bar_in in;
	struct tenstorrent_map_peer_bar_out out;
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

struct tenstorrent_set_noc_cleanup {
	__u32 argsz;
	__u32 flags;
	__u8 enabled;
	__u8 x;
	__u8 y;
	__u8 noc;
	__u32 reserved0;
	__u64 addr;
	__u64 data;
};

#endif // TTDRIVER_IOCTL_H_INCLUDED


// Constants
constexpr size_t TLB_WINDOW_SIZE_2M = 2 * 1024 * 1024;

// Forward declarations for helper functions
void fill_with_random_data(void* ptr, size_t bytes);
bool get_pcie_coords(int fd, uint16_t& out_x, uint16_t& out_y);
bool noc_write(int fd, uint16_t x, uint16_t y, uint64_t dest_addr, const void* src, size_t len);
bool run_test(const std::string& test_name, int dev_fd, size_t buffer_size, void* user_mem, uint64_t noc_addr);


/**
 * @brief Test using a buffer allocated by the kernel driver.
 * @return true on success, false on failure.
 */
bool test_with_driver_allocated_buffer(int dev_fd, size_t buffer_size) {
    std::cout << "\n--- Running Test with Driver-Allocated Buffer ---" << std::endl;
    void* mapped_dma_buf = MAP_FAILED;
    bool success = false;

    // 1. Ask kernel to allocate a DMA buffer.
    tenstorrent_allocate_dma_buf dma_alloc_cmd = {};
    dma_alloc_cmd.in.requested_size = buffer_size;
    dma_alloc_cmd.in.buf_index = 0; // Using buffer index 0
    dma_alloc_cmd.in.flags = TENSTORRENT_ALLOCATE_DMA_BUF_NOC_DMA;

    if (ioctl(dev_fd, TENSTORRENT_IOCTL_ALLOCATE_DMA_BUF, &dma_alloc_cmd) != 0) {
        std::cerr << "IOCTL_ALLOCATE_DMA_BUF failed: " << strerror(errno) << std::endl;
        return false;
    }

    uint64_t noc_target_addr = dma_alloc_cmd.out.noc_address;
    uint64_t mmap_offset = dma_alloc_cmd.out.mapping_offset;
    size_t allocated_size = dma_alloc_cmd.out.size;

    std::cout << "Driver allocated DMA buffer of size " << allocated_size << " bytes." << std::endl;
    std::cout << "  -> NOC Address: 0x" << std::hex << noc_target_addr << std::dec << std::endl;
    std::cout << "  -> MMAP Offset: 0x" << std::hex << mmap_offset << std::dec << std::endl;

    // 2. Memory-map the buffer into this process's address space.
    mapped_dma_buf = mmap(NULL, allocated_size, PROT_READ | PROT_WRITE, MAP_SHARED, dev_fd, mmap_offset);
    if (mapped_dma_buf == MAP_FAILED) {
        std::cerr << "mmap failed for DMA buffer: " << std::string(strerror(errno)) << std::endl;
    } else {
        // 3. Run the core test logic.
        success = run_test("Driver-Allocated", dev_fd, allocated_size, mapped_dma_buf, noc_target_addr);
        munmap(mapped_dma_buf, allocated_size);
    }

    return success;
}

/**
 * @brief Test using a buffer allocated in user space and pinned.
 * @return true on success, false on failure.
 */
bool test_with_user_pinned_buffer(int dev_fd, size_t buffer_size) {
    std::cout << "\n--- Running Test with User-Pinned Buffer ---" << std::endl;
    void* user_mem = MAP_FAILED;
    bool success = false;
    long page_size = sysconf(_SC_PAGESIZE);
    size_t aligned_size = (buffer_size + page_size - 1) & ~(page_size - 1);

    // 1. Allocate page-aligned memory in user space.
    user_mem = mmap(NULL, aligned_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (user_mem == MAP_FAILED) {
        std::cerr << "Failed to mmap user buffer: " << strerror(errno) << std::endl;
        return false;
    }
    std::cout << "User-space mmap'd " << aligned_size << " bytes at VA " << user_mem << std::endl;

    // 2. Ask kernel to "pin" this memory and provide a NOC address.
    struct {
        struct tenstorrent_pin_pages_in in;
        struct tenstorrent_pin_pages_out_extended out;
    } pin_pages_cmd;

    memset(&pin_pages_cmd, 0, sizeof(pin_pages_cmd));
    pin_pages_cmd.in.output_size_bytes = sizeof(pin_pages_cmd.out);
    pin_pages_cmd.in.virtual_address = (uint64_t)user_mem;
    pin_pages_cmd.in.size = aligned_size;
    pin_pages_cmd.in.flags = TENSTORRENT_PIN_PAGES_NOC_DMA;

    if (ioctl(dev_fd, TENSTORRENT_IOCTL_PIN_PAGES, &pin_pages_cmd) != 0) {
        std::cerr << "IOCTL_PIN_PAGES failed: " << strerror(errno) << std::endl;
        munmap(user_mem, aligned_size);
        return false;
    }

    uint64_t noc_target_addr = pin_pages_cmd.out.noc_address;
    std::cout << "Pinned user buffer." << std::endl;
    std::cout << "  -> NOC Address: 0x" << std::hex << noc_target_addr << std::dec << std::endl;

    // 3. Run the core test logic.
    success = run_test("User-Pinned", dev_fd, aligned_size, user_mem, noc_target_addr);

    // 4. Unpin the memory.
    tenstorrent_unpin_pages unpin_cmd = {};
    unpin_cmd.in.virtual_address = (uint64_t)user_mem;
    unpin_cmd.in.size = aligned_size;
    if (ioctl(dev_fd, TENSTORRENT_IOCTL_UNPIN_PAGES, &unpin_cmd) != 0) {
        std::cerr << "Warning: IOCTL_UNPIN_PAGES failed: " << strerror(errno) << std::endl;
        success = false;
    }

    munmap(user_mem, aligned_size);
    return success;
}


/**
 * @brief Main entry point for the test program.
 */
int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " <device_path> <size_in_num_pages>" << std::endl;
        std::cerr << "Example: " << argv[0] << " /dev/tenstorrent/0 1" << std::endl;
        return 1;
    }

    const char* device_path = argv[1];
    const size_t buffer_size = std::stoul(argv[2]) * 0x1000;
    int dev_fd = -1;
    int return_code = 1; // Default to failure

    try {
        dev_fd = open(device_path, O_RDWR | O_CLOEXEC);
        if (dev_fd < 0) {
            throw std::runtime_error("Failed to open device " + std::string(device_path) + ": " + strerror(errno));
        }
        std::cout << "Successfully opened device: " << device_path << std::endl;

        bool driver_alloc_ok = test_with_driver_allocated_buffer(dev_fd, buffer_size);
        bool user_pinned_ok = test_with_user_pinned_buffer(dev_fd, buffer_size);

        if (driver_alloc_ok && user_pinned_ok) {
            std::cout << "\n********************************" << std::endl;
            std::cout <<   "*** ALL TESTS PASSED         ***" << std::endl;
            std::cout <<   "********************************" << std::endl;
            return_code = 0;
        } else {
            std::cout << "\n********************************" << std::endl;
            std::cout <<   "*** ONE OR MORE TESTS FAILED ***" << std::endl;
            std::cout <<   "********************************" << std::endl;
            return_code = 1;
        }

    } catch (const std::exception& e) {
        std::cerr << "A critical error occurred: " << e.what() << std::endl;
        return_code = 1;
    }

    if (dev_fd >= 0) {
        close(dev_fd);
    }

    return return_code;
}

#if 0
/**
 * @brief The core test logic, independent of buffer allocation method.
 */
bool run_test(const std::string& test_name, int dev_fd, size_t buffer_size, void* user_mem, uint64_t noc_addr) {
    // 1. Prepare a source buffer with random data.
    std::vector<uint8_t> source_pattern(buffer_size);
    fill_with_random_data(source_pattern.data(), buffer_size);
    std::cout << "Generated random data pattern." << std::endl;

    // 2. Get the coordinates of the PCIe tile to target the NOC write.
    uint16_t pcie_x, pcie_y;
    if (!get_pcie_coords(dev_fd, pcie_x, pcie_y)) {
        throw std::runtime_error("Failed to get PCIe coordinates.");
    }
    std::cout << "PCIe coordinates: (" << pcie_x << ", " << pcie_y << ")" << std::endl;

    // 3. Perform the NOC write.
    std::cout << "Performing NOC write to 0x" << std::hex << noc_addr << std::dec << "..." << std::endl;
    if (!noc_write(dev_fd, pcie_x, pcie_y, noc_addr, source_pattern.data(), buffer_size)) {
        throw std::runtime_error("NOC write operation failed.");
    }
    std::cout << "NOC write completed." << std::endl;

    // 4. Verify the data.
    std::cout << "Verifying data..." << std::endl;
    if (memcmp(user_mem, source_pattern.data(), buffer_size) == 0) {
        std::cout << "SUCCESS: Data verification passed for " << test_name << " test!" << std::endl;
        return true;
    } else {
        std::cerr << "FAILURE: Data mismatch detected for " << test_name << " test!" << std::endl;
        // Optional: Print mismatch details for debugging
        // for (size_t i = 0; i < std::min((size_t)32, buffer_size); ++i) {
        //     std::cerr << "Byte " << i << ": Expected " << (int)source_pattern[i] << ", Got " << (int)((uint8_t*)user_mem)[i] << std::endl;
        // }
        return false;
    }
}
#endif

/**
 * @brief The core test logic, based on the v3 random-write test.
 * This function fills a buffer with a known pattern, randomly zeroes out
 * some 32-bit words via NOC writes, and then verifies the entire buffer.
 */
bool run_test(const std::string& test_name, int dev_fd, size_t buffer_size, void* user_mem, uint64_t noc_addr) {
    // Ensure the buffer is suitable for 32-bit word operations.
    if (buffer_size % sizeof(uint32_t) != 0) {
        std::cerr << "FAILURE: Buffer size " << buffer_size << " is not a multiple of 4 for " << test_name << " test!" << std::endl;
        return false;
    }

    uint32_t* base = static_cast<uint32_t*>(user_mem);
    const size_t nwords = buffer_size / sizeof(uint32_t);

    // 1. Fill the buffer with a known, non-zero pattern.
    std::cout << "Filling buffer with initial pattern..." << std::endl;
    for (size_t i = 0; i < nwords; i++) {
        base[i] = 0xDEADBEEF ^ (uint32_t)i;
    }

    // 2. Get PCIe coordinates for the NOC writes.
    uint16_t pcie_x, pcie_y;
    if (!get_pcie_coords(dev_fd, pcie_x, pcie_y)) {
        // get_pcie_coords already prints an error, so we just return.
        return false;
    }
    std::cout << "PCIe coordinates: (" << pcie_x << ", " << pcie_y << ")" << std::endl;

    // 3. Randomly choose some word indices to zero out via device NOC writes.
    std::mt19937_64 rng(0x12345678); // Use a fixed seed for reproducible tests
    std::uniform_int_distribution<size_t> dist(0, nwords - 1);
    std::unordered_set<size_t> zeroed_indices;

    const size_t num_ops = std::min<size_t>(nwords, 256); // Limit how many writes to perform
    std::cout << "Performing " << num_ops << " random 32-bit NOC writes to zero out words..." << std::endl;

    for (size_t i = 0; i < num_ops; i++) {
        size_t idx = dist(rng);
        uint64_t addr = noc_addr + idx * sizeof(uint32_t);
        uint32_t zero = 0;

        // Use the existing noc_write helper to write a single 4-byte word.
        if (!noc_write(dev_fd, pcie_x, pcie_y, addr, &zero, sizeof(uint32_t))) {
            std::cerr << "FAILURE: noc_write failed for word at index " << idx << " for " << test_name << " test!" << std::endl;
            return false;
        }

        zeroed_indices.insert(idx);
    }
    std::cout << "NOC writes completed. " << zeroed_indices.size() << " unique words were targeted." << std::endl;


    // 4. Verify the entire buffer.
    std::cout << "Verifying data..." << std::endl;
    for (size_t i = 0; i < nwords; i++) {
        uint32_t expected_value = (zeroed_indices.count(i) > 0) ? 0 : (0xDEADBEEF ^ (uint32_t)i);
        if (base[i] != expected_value) {
            std::cerr << "FAILURE: Data mismatch for " << test_name << " test at word index " << i << "!" << std::endl;
            std::cerr << "  -> Expected: 0x" << std::hex << expected_value << std::dec << std::endl;
            std::cerr << "  -> Got:      0x" << std::hex << base[i] << std::dec << std::endl;
            return false;
        }
    }

    std::cout << "SUCCESS: Data verification passed for " << test_name << " test!" << std::endl;
    return true;
}



/**
 * @brief Fills a memory buffer with pseudorandom data.
 */
void fill_with_random_data(void* ptr, size_t bytes) {
    if (bytes == 0) return;
    static std::mt19937_64 eng(0xCAFEF00D); // Use fixed seed for reproducibility
    size_t num_uint64 = bytes / sizeof(uint64_t);
    size_t rem_bytes = bytes % sizeof(uint64_t);
    uint64_t* ptr64 = static_cast<uint64_t*>(ptr);
    for (size_t i = 0; i < num_uint64; ++i) {
        ptr64[i] = eng();
    }
    if (rem_bytes > 0) {
        uint8_t* ptr8 = reinterpret_cast<uint8_t*>(ptr64 + num_uint64);
        uint64_t last_chunk = eng();
        memcpy(ptr8, &last_chunk, rem_bytes);
    }
}

/**
 * @brief Queries the device for its architecture to determine PCIe coordinates.
 */
bool get_pcie_coords(int fd, uint16_t& out_x, uint16_t& out_y) {
    tenstorrent_get_device_info info_cmd = {};
    info_cmd.in.output_size_bytes = sizeof(info_cmd.out);

    if (ioctl(fd, TENSTORRENT_IOCTL_GET_DEVICE_INFO, &info_cmd) != 0) {
        std::cerr << "IOCTL_GET_DEVICE_INFO failed: " << strerror(errno) << std::endl;
        return false;
    }

    if (info_cmd.out.device_id == 0x401e) { // Wormhole
        out_x = 0;
        out_y = 3;
        return true;
    } else if (info_cmd.out.device_id == 0xb140) { // Blackhole
        out_x = 19;
        out_y = 24;
        return true;
    }

    std::cerr << "Unknown device ID: 0x" << std::hex << info_cmd.out.device_id << std::dec << std::endl;
    return false;
}

/**
 * @brief Writes a block of data to a specific NOC address on the device.
 */
bool noc_write(int fd, uint16_t x, uint16_t y, uint64_t dest_addr, const void* src, size_t len) {
    if ((dest_addr % 4 != 0) || (len % 4 != 0)) {
        std::cerr << "NOC write requires 4-byte aligned address and length." << std::endl;
        return false;
    }

    const uint8_t* src_ptr = static_cast<const uint8_t*>(src);
    uint64_t current_addr = dest_addr;
    size_t remaining_len = len;

    while (remaining_len > 0) {
        // 1. Allocate a TLB
        tenstorrent_allocate_tlb tlb_alloc_cmd = {};
        tlb_alloc_cmd.in.size = TLB_WINDOW_SIZE_2M;
        if (ioctl(fd, TENSTORRENT_IOCTL_ALLOCATE_TLB, &tlb_alloc_cmd) != 0) {
            std::cerr << "IOCTL_ALLOCATE_TLB failed: " << strerror(errno) << std::endl;
            return false;
        }

        // 2. Mmap the TLB window (using Write-Combined caching)
        void* tlb_mmio = mmap(NULL, TLB_WINDOW_SIZE_2M, PROT_READ | PROT_WRITE, MAP_SHARED, fd, tlb_alloc_cmd.out.mmap_offset_wc);
        if (tlb_mmio == MAP_FAILED) {
            std::cerr << "mmap failed for TLB: " << strerror(errno) << std::endl;
            // Best-effort cleanup of the allocated TLB
            tenstorrent_free_tlb tlb_free_cmd = {};
            tlb_free_cmd.in.id = tlb_alloc_cmd.out.id;
            ioctl(fd, TENSTORRENT_IOCTL_FREE_TLB, &tlb_free_cmd);
            return false;
        }

        // 3. Configure the TLB to point to the correct NOC address
        uint64_t aligned_addr = current_addr & ~(TLB_WINDOW_SIZE_2M - 1);
        tenstorrent_configure_tlb tlb_config_cmd = {};
        tlb_config_cmd.in.id = tlb_alloc_cmd.out.id;
        tlb_config_cmd.in.config.addr = aligned_addr;
        tlb_config_cmd.in.config.x_end = x;
        tlb_config_cmd.in.config.y_end = y;
        // Other fields default to 0 for a simple unicast write

        if (ioctl(fd, TENSTORRENT_IOCTL_CONFIGURE_TLB, &tlb_config_cmd) != 0) {
            std::cerr << "IOCTL_CONFIGURE_TLB failed: " << strerror(errno) << std::endl;
            munmap(tlb_mmio, TLB_WINDOW_SIZE_2M);
            tenstorrent_free_tlb tlb_free_cmd = {};
            tlb_free_cmd.in.id = tlb_alloc_cmd.out.id;
            ioctl(fd, TENSTORRENT_IOCTL_FREE_TLB, &tlb_free_cmd);
            return false;
        }

        // 4. Copy data into the mapped window using volatile 32-bit stores.
        uint64_t offset_in_window = current_addr & (TLB_WINDOW_SIZE_2M - 1);
        size_t chunk_size = std::min(remaining_len, TLB_WINDOW_SIZE_2M - offset_in_window);

        volatile uint32_t* dest32 = reinterpret_cast<volatile uint32_t*>(static_cast<uint8_t*>(tlb_mmio) + offset_in_window);
        const uint32_t* src32 = reinterpret_cast<const uint32_t*>(src_ptr);

        for (size_t i = 0; i < chunk_size / sizeof(uint32_t); ++i) {
            dest32[i] = src32[i];
        }

        // 5. Cleanup for this chunk
        munmap(tlb_mmio, TLB_WINDOW_SIZE_2M);
        tenstorrent_free_tlb tlb_free_cmd = {};
        tlb_free_cmd.in.id = tlb_alloc_cmd.out.id;
        if (ioctl(fd, TENSTORRENT_IOCTL_FREE_TLB, &tlb_free_cmd) != 0) {
            std::cerr << "Warning: IOCTL_FREE_TLB failed: " << strerror(errno) << std::endl;
            // Continue anyway, but this could indicate a problem.
        }

        // 6. Update pointers for the next iteration
        src_ptr += chunk_size;
        current_addr += chunk_size;
        remaining_len -= chunk_size;
    }

    return true;
}

