#pragma once

#include "logger.hpp"

#include <chrono>
#include <cstdint>
#include <optional>
#include <sstream>
#include <string>

#include <unistd.h>
#include <sys/mman.h>


void write_file(const std::string& filename, const void* data, size_t size);
std::string read_file(const std::string& filename);
void fill_with_random_data(void* ptr, size_t bytes);
int32_t get_number_of_hugepages_free();
int32_t get_number_of_hugepages_total();

template <typename T>
std::optional<T> read_small_file(const std::string& path)
{
    try {
        std::string data = read_file(path);
        if (data.empty()) {
            return std::nullopt;
        }

        std::stringstream ss(data);
        T value;
        if (!(ss >> value)) {
            return std::nullopt;
        }
        return value;
    } catch (...) {
        return std::nullopt;
    }
}

// Simple timer for basic benchmarking.
class Timer
{
    std::chrono::time_point<std::chrono::steady_clock> start_time;

public:
    Timer()
        : start_time(std::chrono::steady_clock::now())
    {
    }

    uint64_t elapsed_ns() const
    {
        auto end_time = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time).count();
    }

    uint64_t elapsed_us() const
    {
        return elapsed_ns() / 1'000;
    }

    uint64_t elapsed_ms() const
    {
        return elapsed_ns() / 1'000'000;
    }

    uint64_t elapsed_s() const
    {
        return elapsed_ns() / 1'000'000'000;
    }

    void reset()
    {
        start_time = std::chrono::steady_clock::now();
    }
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

    uint32_t read32(uint64_t offset)
    {
        if (offset & (sizeof(uint32_t) - 1)) {
            RUNTIME_ERROR("Memory access must be aligned");
        }

        if (offset + sizeof(uint32_t) > mem_size) {
            RUNTIME_ERROR("Memory access out of bounds");
        }

        return *reinterpret_cast<volatile uint32_t*>(mem + offset);
    }

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