// SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
// SPDX-License-Identifier: GPL-2.0-only WITH Linux-syscall-note
//
// This is a standalone C++ program to test NOC DMA functionality.
// It allocates a userspace buffer, uses the TENSTORRENT_IOCTL_PIN_PAGES ioctl
// to make it device-accessible, and then verifies data transfer to it via a NOC
// write.

#include <iostream>
#include <vector>
#include <random>
#include <cstring>
#include <cerrno>
#include <stdexcept>
#include <algorithm>
#include <cstdlib>

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include <linux/types.h>

// --- Start of inlined ioctl.h definitions ---
#define TENSTORRENT_IOCTL_MAGIC 0xFA

#define TENSTORRENT_IOCTL_GET_DEVICE_INFO	_IO(TENSTORRENT_IOCTL_MAGIC, 0)
#define TENSTORRENT_IOCTL_PIN_PAGES		_IO(TENSTORRENT_IOCTL_MAGIC, 7)
#define TENSTORRENT_IOCTL_UNPIN_PAGES		_IO(TENSTORRENT_IOCTL_MAGIC, 10)
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

// tenstorrent_pin_pages_in.flags
#define TENSTORRENT_PIN_PAGES_CONTIGUOUS 1
#define TENSTORRENT_PIN_PAGES_NOC_DMA 2
#define TENSTORRENT_PIN_PAGES_NOC_TOP_DOWN 4

struct tenstorrent_pin_pages_in {
	__u32 output_size_bytes;
	__u32 flags;
	__u64 virtual_address;
	__u64 size;
};

struct tenstorrent_pin_pages_out_extended {
	__u64 physical_address;	// or IOVA
	__u64 noc_address;
};

struct tenstorrent_pin_pages_extended {
	struct tenstorrent_pin_pages_in in;
	struct tenstorrent_pin_pages_out_extended out;
};

struct tenstorrent_unpin_pages_in {
	__u64 virtual_address;
	__u64 size;
	__u64 reserved;
};

struct tenstorrent_unpin_pages_out {};

struct tenstorrent_unpin_pages {
	struct tenstorrent_unpin_pages_in in;
	struct tenstorrent_unpin_pages_out out;
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

struct tenstorrent_free_tlb_out {};

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
// --- End of inlined ioctl.h definitions ---

// Constants
constexpr size_t TLB_WINDOW_SIZE_2M = 2 * 1024 * 1024;
constexpr size_t PAGE_SIZE = 4096;

// Forward declarations for helper functions
void fill_with_random_data(void* ptr, size_t bytes);
bool get_pcie_coords(int fd, uint16_t& out_x, uint16_t& out_y);
bool noc_write(int fd, uint16_t x, uint16_t y, uint64_t dest_addr, const void* src, size_t len);

/**
 * @brief Main entry point for the test program.
 */
int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " <device_path> <size_in_bytes>" << std::endl;
        std::cerr << "Example: " << argv[0] << " /dev/tenstorrent/0 4096" << std::endl;
        std::cerr << "Example: " << argv[0] << " /dev/tenstorrent/0 0x1000" << std::endl;
        return 1;
    }

    const char* device_path = argv[1];
    const size_t buffer_size = std::stoul(argv[2], nullptr, 0);
    int dev_fd = -1;
    void* host_buf = nullptr;
    int return_code = 1; // Default to failure

    try {
        // 1. Open the Tenstorrent device
        dev_fd = open(device_path, O_RDWR | O_CLOEXEC);
        if (dev_fd < 0) {
            throw std::runtime_error("Failed to open device " + std::string(device_path) + ": " + strerror(errno));
        }
        std::cout << "Successfully opened device: " << device_path << std::endl;

        // 2. Allocate a page-aligned buffer in userspace.
        if (posix_memalign(&host_buf, PAGE_SIZE, buffer_size) != 0) {
            throw std::runtime_error("Failed to allocate page-aligned memory: " + std::string(strerror(errno)));
        }
        std::cout << "Allocated " << buffer_size << " bytes of host memory at " << host_buf << std::endl;

        // 3. Pin the userspace buffer to make it accessible to the device.
        tenstorrent_pin_pages_extended pin_cmd = {};
        pin_cmd.in.virtual_address = reinterpret_cast<uint64_t>(host_buf);
        pin_cmd.in.size = buffer_size;
        pin_cmd.in.flags = TENSTORRENT_PIN_PAGES_NOC_DMA;
        pin_cmd.in.output_size_bytes = sizeof(pin_cmd.out);

        if (ioctl(dev_fd, TENSTORRENT_IOCTL_PIN_PAGES, &pin_cmd) != 0) {
            throw std::runtime_error("IOCTL_PIN_PAGES failed: " + std::string(strerror(errno)));
        }

        uint64_t noc_target_addr = pin_cmd.out.noc_address;
        uint64_t iova = pin_cmd.out.physical_address;

        std::cout << "Pinned host buffer." << std::endl;
        std::cout << "  -> NOC Address: 0x" << std::hex << noc_target_addr << std::dec << std::endl;
        std::cout << "  -> IOVA       : 0x" << std::hex << iova << std::dec << std::endl;

        // 4. Prepare a source buffer with random data.
        std::vector<uint8_t> source_pattern(buffer_size);
        fill_with_random_data(source_pattern.data(), buffer_size);
        std::cout << "Generated random data pattern." << std::endl;

        // 5. Get the coordinates of the PCIe tile to target the NOC write.
        uint16_t pcie_x, pcie_y;
        if (!get_pcie_coords(dev_fd, pcie_x, pcie_y)) {
            throw std::runtime_error("Failed to get PCIe coordinates.");
        }
        std::cout << "PCIe coordinates: (" << pcie_x << ", " << pcie_y << ")" << std::endl;

        // 6. Perform the NOC write. This transfers data from our user-space `source_pattern`
        // buffer to the device, which should write it into our pinned `host_buf`.
        std::cout << "Performing NOC write..." << std::endl;
        if (!noc_write(dev_fd, pcie_x, pcie_y, noc_target_addr, source_pattern.data(), buffer_size)) {
            throw std::runtime_error("NOC write operation failed.");
        }
        std::cout << "NOC write completed." << std::endl;

        // 7. Verify the data. Compare the host buffer with the original source pattern.
        std::cout << "Verifying data..." << std::endl;
        if (memcmp(host_buf, source_pattern.data(), buffer_size) == 0) {
            std::cout << "\nSUCCESS: Data verification passed!" << std::endl;
            return_code = 0; // Success
        } else {
            std::cerr << "\nFAILURE: Data mismatch detected!" << std::endl;
        }

    } catch (const std::exception& e) {
        std::cerr << "An error occurred: " << e.what() << std::endl;
        return_code = 1;
    }

    // 8. Cleanup
    if (host_buf != nullptr) {
        // Unpin the buffer
        tenstorrent_unpin_pages unpin_cmd = {};
        unpin_cmd.in.virtual_address = reinterpret_cast<uint64_t>(host_buf);
        unpin_cmd.in.size = buffer_size;
        if (ioctl(dev_fd, TENSTORRENT_IOCTL_UNPIN_PAGES, &unpin_cmd) != 0) {
            std::cerr << "Warning: IOCTL_UNPIN_PAGES failed: " << strerror(errno) << std::endl;
        } else {
            std::cout << "Successfully unpinned host buffer." << std::endl;
        }
        free(host_buf);
    }
    if (dev_fd >= 0) {
        close(dev_fd);
    }

    return return_code;
}


/**
 * @brief Fills a memory buffer with pseudorandom data.
 * @param ptr Pointer to the buffer.
 * @param bytes The number of bytes to fill.
 */
void fill_with_random_data(void* ptr, size_t bytes) {
    if (bytes == 0) return;
    static std::mt19937_64 eng(std::random_device{}());
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
 * @param fd Open file descriptor to the device.
 * @param out_x Output for the PCIe X coordinate.
 * @param out_y Output for the PCIe Y coordinate.
 * @return true on success, false on failure.
 */
bool get_pcie_coords(int fd, uint16_t& out_x, uint16_t& out_y) {
    tenstorrent_get_device_info info_cmd = {};
    info_cmd.in.output_size_bytes = sizeof(info_cmd.out);

    if (ioctl(fd, TENSTORRENT_IOCTL_GET_DEVICE_INFO, &info_cmd) != 0) {
        std::cerr << "IOCTL_GET_DEVICE_INFO failed: " << strerror(errno) << std::endl;
        return false;
    }

    // Determine architecture from PCI device ID to get coordinates.
    // These are hardcoded values specific to Tenstorrent architectures.
    if (info_cmd.out.device_id == 0x401e) { // Wormhole
        out_x = 0;
        out_y = 3;
        return true;
    } else if (info_cmd.out.device_id == 0xb140) { // Blackhole
        // Correct coordinates as per holething.hpp
        out_x = 19;
        out_y = 24;
        return true;
    }

    std::cerr << "Unknown device ID: 0x" << std::hex << info_cmd.out.device_id << std::dec << std::endl;
    return false;
}

/**
 * @brief Writes a block of data to a specific NOC address on the device.
 * This function handles the complexity of allocating, mapping, and configuring
 * TLB windows to access the device's memory space.
 * @param fd Open file descriptor to the device.
 * @param x The NOC X coordinate.
 * @param y The NOC Y coordinate.
 * @param dest_addr The destination NOC address.
 * @param src Pointer to the source data buffer.
 * @param len The number of bytes to write.
 * @return true on success, false on failure.
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

        // 4. Copy data into the mapped window
        uint64_t offset_in_window = current_addr & (TLB_WINDOW_SIZE_2M - 1);
        size_t chunk_size = std::min(remaining_len, TLB_WINDOW_SIZE_2M - offset_in_window);
        memcpy(static_cast<uint8_t*>(tlb_mmio) + offset_in_window, src_ptr, chunk_size);

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
