#include "device.hpp"

#define DIE(msg) do { \
    perror(msg); \
    exit(EXIT_FAILURE); \
} while (0)

#if 0
#endif

void noc_read(const char* path,
              uint16_t x,
              uint16_t y,
              uint64_t addr,
              void* dst,
              size_t size)
{
    if (size < 4) {
        DIE("Size must be at least 4 bytes");
    }

    if (size % 4 != 0) {
        DIE("Size must be a multiple of 4 bytes");
    }

    int fd = open(path, O_RDWR);
    if (fd == -1) {
        DIE("Failed to open file");
    }

    size_t window_size = (1 << 21);
    tenstorrent_allocate_tlb allocate_tlb{};
    allocate_tlb.in.size = window_size;
    if (ioctl(fd, TENSTORRENT_IOCTL_ALLOCATE_TLB, &allocate_tlb) != 0) {
        DIE("Failed to allocate TLB");
    }

    size_t window_mask = (window_size) - 1;
    bool write_combined = (size > 4);
    auto mmap_offset = (write_combined ? allocate_tlb.out.mmap_offset_wc : allocate_tlb.out.mmap_offset_uc);
    void* mem = mmap(nullptr, window_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, mmap_offset);
    if (mem == MAP_FAILED) {
        DIE("Failed to map TLB");
    }

    while (size > 0) {
        const uint64_t addr_aligned = addr & ~window_mask;
        const uint64_t offset = addr & window_mask;
        const size_t apparent_size = window_size - offset;
        const size_t chunk_size = std::min(size, apparent_size);
        const tenstorrent_noc_tlb_config config{
            .addr = addr_aligned,
            .x_end = x,
            .y_end = y,
        };

        tenstorrent_configure_tlb configure_tlb{};
        configure_tlb.in.id = allocate_tlb.out.id;
        configure_tlb.in.config = config;
        if (ioctl(fd, TENSTORRENT_IOCTL_CONFIGURE_TLB, &configure_tlb) != 0) {
            DIE("Failed to configure TLB");
        }

        uint8_t* src = (uint8_t*)mem + offset;
        memcpy(dst, src, chunk_size);

        dst = (uint8_t*)dst + chunk_size;
        size = size - chunk_size;
        addr = addr +chunk_size;
    }

    munmap(mem, size);
    close(fd);
}

int main()
{
    uint32_t value;
    noc_read("/dev/tenstorrent/0", 2, 11, 0xffb20148ULL, &value, sizeof(value));
    printf("value: %08x\n", value);
    return 0;
}

int old()
{
    for (auto device_path : Device::enumerate_devices()) {
        Device device(device_path);

        if (device.is_wormhole()) {
            LOG_INFO("Wormhole device found");
        } else if (device.is_blackhole()) {
            LOG_INFO("Blackhole device found");

            auto window = device.map_tlb_4G(8, 3, 0x400030000000ULL, CacheMode::WriteCombined);
            std::memset((void*)window->get_mem(), 0x55, 1 << 30);

        }
    }
    return 0;
}