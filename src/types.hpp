#pragma once

#include "logger.hpp"

#include <cstdint>
#include <span>
#include <utility>

#include <sys/mman.h>

enum CacheMode { Uncached, WriteCombined };
enum CardType { WORMHOLE, BLACKHOLE };

struct coord_t
{
    uint32_t x;
    uint32_t y;
};

// Owning wrapper for a memory region.
class MappedMemory
{
    uint8_t* mem;
    size_t mem_size;

public:
    MappedMemory(uint8_t* mem, size_t size)
        : mem(mem)
        , mem_size(size)
    {
    }

    MappedMemory(const MappedMemory&) = delete;
    MappedMemory& operator=(const MappedMemory&) = delete;
    MappedMemory(MappedMemory&&) = default;
    MappedMemory& operator=(MappedMemory&&) = default;

    void write32(uint64_t offset, uint32_t value)
    {
        if (offset & (sizeof(value) - 1)) {
            RUNTIME_ERROR("Memory access must be aligned");
        }

        if (offset + sizeof(value) > mem_size) {
            RUNTIME_ERROR("Memory access out of bounds");
        }
        *reinterpret_cast<volatile uint32_t*>(mem + offset) = value;
    }

    size_t get_size() const
    {
        return mem_size;
    }

    template <typename T> T* as_ptr(uint64_t offset = 0)
    {
        return reinterpret_cast<T*>(mem + offset);
    }

    template <typename T> std::span<T> as_span()
    {
        return std::span<T>(as_ptr<T>(), get_size() / sizeof(T));
    }

    ~MappedMemory()
    {
        if (mem) {
            munmap(mem, mem_size);
        }
    }
};

namespace detail {

template <typename Func>
class ScopeGuard
{
public:
    template <typename F>
    explicit ScopeGuard(F&& f)
        : func(std::forward<F>(f))
    {
    }

    ~ScopeGuard() noexcept
    {
        try {
            func();
        } catch (...) {
            LOG_ERROR("Probably should have used Rust LOL");
        }
    }

    ScopeGuard(const ScopeGuard&) = delete;
    ScopeGuard& operator=(const ScopeGuard&) = delete;
    ScopeGuard(ScopeGuard&&) = delete;
    ScopeGuard& operator=(ScopeGuard&&) = delete;

private:
    Func func;
};

enum class ScopeGuardOnExit {};
template <typename Func>
ScopeGuard<Func> operator+(ScopeGuardOnExit, Func&& f)
{
    return ScopeGuard<Func>(std::forward<Func>(f));
}
} // namespace detail

#define DEFER_CONCAT_IMPL(s1, s2) s1##s2
#define DEFER_CONCAT(s1, s2) DEFER_CONCAT_IMPL(s1, s2)
#define DEFER_ANON_VARIABLE(str) DEFER_CONCAT(str, __COUNTER__)
#define DEFER auto DEFER_ANON_VARIABLE(SCOPE_EXIT_STATE) = detail::ScopeGuardOnExit() + [&]() noexcept
