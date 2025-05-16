#include "utility.hpp"

#include <fstream>
#include <random>

void write_file(const std::string& filename, const void* data, size_t size)
{
    std::ofstream file(filename, std::ios::binary);
    file.write(reinterpret_cast<const char*>(data), size);
}

std::string read_file(const std::string& filename)
{
    std::ifstream file(filename, std::ios::binary);
    file.seekg(0, std::ios::end);

    size_t size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::string buffer(size, 0x0);
    file.read(buffer.data(), size);

    return buffer;
}

void fill_with_random_data(void* ptr, size_t bytes)
{
    if (bytes == 0)
        return;

    static std::mt19937_64 eng(std::random_device{}());

    size_t num_uint64 = bytes / sizeof(uint64_t);
    size_t rem_bytes = bytes % sizeof(uint64_t);

    uint64_t* ptr64 = static_cast<uint64_t*>(ptr);
    for (size_t i = 0; i < num_uint64; ++i) {
        ptr64[i] = eng();
    }

    if (rem_bytes > 0) {
        uint8_t* ptr8 = reinterpret_cast<uint8_t*>(ptr64 + num_uint64);
        uint64_t last_random_chunk = eng();
        unsigned char* random_bytes_for_remainder = reinterpret_cast<unsigned char*>(&last_random_chunk);
        for (size_t i = 0; i < rem_bytes; ++i) {
            ptr8[i] = random_bytes_for_remainder[i];
        }
    }
}

int32_t get_number_of_hugepages_free()
{
    static const std::string hugepage_path = "/sys/kernel/mm/hugepages/hugepages-1048576kB/free_hugepages";
    auto n = read_small_file<int32_t>(hugepage_path);
    return n.value_or(0);
}

int32_t get_number_of_hugepages_total()
{
    static const std::string hugepage_path = "/sys/kernel/mm/hugepages/hugepages-1048576kB/nr_hugepages";
    auto n = read_small_file<int32_t>(hugepage_path);
    return n.value_or(0);
}