#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <vector>
#include <unistd.h>


void write_file(const std::string& filename, const void* data, size_t size);
std::vector<uint8_t> read_file_to_vec(const std::string& filename);

inline std::string read_file_to_str(const std::string& filename)
{
    std::ifstream file(filename, std::ios::binary);
    file.seekg(0, std::ios::end);

    size_t size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::string buffer(size, 0x0);
    file.read(buffer.data(), size);

    return buffer;
}


template <typename T>
std::optional<T> read_small_file(const std::string& path)
{
    try {
        std::string data = read_file_to_str(path);
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


template <typename T>
T* aligned_buffer(size_t num_elements)
{
    const size_t page_size = sysconf(_SC_PAGESIZE);
    T* ptr = static_cast<T*>(std::aligned_alloc(page_size, num_elements * sizeof(T)));
    if (!ptr) {
        throw std::bad_alloc();
    }
    return ptr;
}

template <typename T> T random_integer()
{
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<T> dis(std::numeric_limits<T>::min(), std::numeric_limits<T>::max());
    return dis(gen);
}

template <typename T> std::vector<T> random_vec(size_t num_elements)
{
    std::vector<T> vec(num_elements);
    std::generate(vec.begin(), vec.end(), random_integer<T>);
    return vec;
}

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

inline std::vector<std::string> enumerate_devices()
{
    std::vector<std::string> devices;
    const std::string base_path = "/dev/tenstorrent/";
    
    for (const auto& entry : std::filesystem::directory_iterator(base_path)) {
        if (std::filesystem::is_character_file(entry.path()) || 
            std::filesystem::is_block_file(entry.path())) {
            devices.push_back(entry.path().string());
        }
    }
    
    std::sort(devices.begin(), devices.end());
    return devices;
}