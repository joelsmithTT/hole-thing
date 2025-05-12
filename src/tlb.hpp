#pragma once

#include "ioctl.h"
#include "logger.hpp"
#include "types.hpp"

#include <cstring>
#include <memory>
#include <system_error>

#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

// Represents the hardware resource of PCIe->NOC aperture.
class TlbHandle
{
    int fd;
    int tlb_id;
    void* mem;
    size_t tlb_size;
    tenstorrent_noc_tlb_config tlb_config{};
    int cache_mode;

public:
    TlbHandle(int fd, size_t size, const tenstorrent_noc_tlb_config& config, CacheMode mode = WriteCombined)
        : fd(fd)
        , tlb_id(-1)
        , tlb_size(size)
        , mem(nullptr)
        , tlb_config()
        , cache_mode(mode)
    {
        if (fd < 0) {
            RUNTIME_ERROR("Invalid file descriptor");
        }

        tenstorrent_allocate_tlb allocate_tlb{};
        allocate_tlb.in.size = size;
        if (ioctl(fd, TENSTORRENT_IOCTL_ALLOCATE_TLB, &allocate_tlb) != 0) {
            SYSTEM_ERROR("Failed to allocate TLB");
        }

        tlb_id = allocate_tlb.out.id;

        auto mmap_offset = (mode == Uncached ? allocate_tlb.out.mmap_offset_uc : allocate_tlb.out.mmap_offset_wc);
        mem = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, mmap_offset);
        if (mem == MAP_FAILED) {
            free_tlb();
            SYSTEM_ERROR("Failed to map TLB");
        }

        try {
            configure(config);
        } catch (const std::system_error& e) {
            munmap(mem, size);
            free_tlb();
            throw;
        }
    }

    void configure(const tenstorrent_noc_tlb_config& new_config)
    {
        tenstorrent_configure_tlb configure_tlb{};
        configure_tlb.in.id = tlb_id;
        configure_tlb.in.config = new_config;

        if (std::memcmp(&new_config, &tlb_config, sizeof(new_config)) == 0) {
            return;
        }

        if (ioctl(fd, TENSTORRENT_IOCTL_CONFIGURE_TLB, &configure_tlb) != 0) {
            SYSTEM_ERROR("Failed to configure TLB");
        }

        tlb_config = new_config;
    }

    volatile uint8_t* get_mem()
    {
        return static_cast<volatile uint8_t*>(mem);
    }

    size_t get_size() const
    {
        return tlb_size;
    }

    const tenstorrent_noc_tlb_config& get_config() const
    {
        return tlb_config;
    }

    CacheMode get_cache_mode() const
    {
        return static_cast<CacheMode>(cache_mode);
    }

    ~TlbHandle() noexcept
    {
        munmap(mem, tlb_size);
        free_tlb();
    }

private:
    void free_tlb() noexcept
    {
        tenstorrent_free_tlb free_tlb{};
        free_tlb.in.id = tlb_id;
        ioctl(fd, TENSTORRENT_IOCTL_FREE_TLB, &free_tlb);
    }
};

class TlbWindow
{
    std::unique_ptr<TlbHandle> tlb_handle;
    uint64_t base;

public:
    TlbWindow(std::unique_ptr<TlbHandle> handle, uint64_t base = 0)
        : tlb_handle(std::move(handle))
        , base(base)
    {
        if (!tlb_handle) {
            RUNTIME_ERROR("Invalid TLB handle");
        }
        // LOG_INFO("Created TlbWindow with base: %lx", base);
    }

    uint32_t read32(uint64_t offset)
    {
        uint32_t value;
        read_block(offset, &value, sizeof(value));
        return value;
    }

    void write32(uint64_t offset, uint32_t value)
    {
        write_block(offset, &value, sizeof(value));
    }

    virtual void write_block(uint64_t offset, const void* data, size_t size)
    {
        size_t n = size / sizeof(uint32_t);
        auto* src = static_cast<const uint32_t*>(data);
        auto* dst = reinterpret_cast<volatile uint32_t*>(get_mem() + offset);

        if (offset & (sizeof(uint32_t) - 1)) {
            RUNTIME_ERROR("Memory access must be aligned");
        }

        if (size % sizeof(uint32_t) != 0) {
            RUNTIME_ERROR("Size must be a multiple of 4");
        }

        if (offset + size > get_size()) {
            RUNTIME_ERROR("Memory access out of bounds");
        }

        for (size_t i = 0; i < n; i++) {
            dst[i] = src[i];
        }
    }

    virtual void read_block(uint64_t offset, void* data, size_t size)
    {
        size_t n = size / sizeof(uint32_t);
        auto* src = reinterpret_cast<volatile uint32_t*>(get_mem() + offset);
        auto* dst = static_cast<uint32_t*>(data);

        if (offset & (sizeof(uint32_t) - 1)) {
            RUNTIME_ERROR("Memory access must be aligned");
        }

        if (size % sizeof(uint32_t) != 0) {
            RUNTIME_ERROR("Size must be a multiple of 4");
        }

        if (offset + size > get_size()) {
            RUNTIME_ERROR("Memory access out of bounds");
        }

        for (size_t i = 0; i < n; i++) {
            dst[i] = src[i];
        }
    }

    size_t get_size() const
    {
        return tlb_handle->get_size() - base;
    }

    volatile uint8_t* get_mem() const
    {
        return reinterpret_cast<volatile uint8_t*>(tlb_handle->get_mem() + base);
    }

    template <typename T>
    volatile T* as_ptr()
    {
        return reinterpret_cast<volatile T*>(get_mem());
    }

    ~TlbWindow() noexcept
    {
    }
};
