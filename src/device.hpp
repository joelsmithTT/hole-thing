#pragma once

#include "ioctl.h"
#include "logger.hpp"
#include "pcie.hpp"
#include "tlb.hpp"

#include <cstdint>
#include <memory>
#include <system_error>

#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

class Device
{
public:
    int fd;
    MappedMemory bar2;
    PciDeviceInfo device_info;

    Device(const std::string& chardev_path, uint16_t expected_device_id = 0)
        : fd(open(chardev_path.c_str(), O_RDWR | O_CLOEXEC))
        , bar2(map_bar2(fd))
    {
        if (fd < 0) {
            SYSTEM_ERROR("Failed to open device");
        }

        device_info = get_device_info(fd);

        if (expected_device_id != 0 && device_info.device_id != expected_device_id) {
            close(fd);
            RUNTIME_ERROR("Unexpected device ID: %04x, expected: %04x", device_info.device_id, expected_device_id);
        }
    }

    int get_fd() const
    {
        return fd;
    }

    bool is_wormhole() const
    {
        return device_info.device_id == WORMHOLE_ID;
    }

    bool is_blackhole() const
    {
        return device_info.device_id == BLACKHOLE_ID;
    }

    MappedMemory& get_bar2()
    {
        return bar2;
    }

    coord_t get_pcie_coordinates()
    {
        if (is_wormhole()) {
            return { 0, 3 };
        } else if (is_blackhole()) {
            static constexpr uint64_t NOC_ID_OFFSET = 0x4044;

            auto noc2axi = bh_map_noc2axi(fd);
            auto span = noc2axi.as_span<volatile uint32_t>();
            auto noc_id = span[NOC_ID_OFFSET / sizeof(uint32_t)];
            auto x = (noc_id >> 0x0) & 0x3f;
            auto y = (noc_id >> 0x6) & 0x3f;

            LOG_INFO("Blackhole: host-visible PCIe core is at NOC0 coordinates (x=%u, y=%u)", x, y);

            return {x, y};
        } else {
            throw std::runtime_error("Unknown device type");
        }
    }

    std::unique_ptr<TlbWindow> map_tlb(uint32_t x, uint32_t y, uint64_t address, CacheMode mode, size_t window_size)
    {
        const uint64_t window_mask = window_size - 1;
        const uint64_t addr = address & ~window_mask;
        const uint64_t offset = address & window_mask;
        const tenstorrent_noc_tlb_config config{
            .addr = addr,
            .x_end = x,
            .y_end = y,
        };

        LOG_DEBUG("Mapping TLB window: x=%u, y=%u, address=0x%lx, offset=0x%lx, mode=%d", x, y, addr, offset, mode);
        auto handle = std::make_unique<TlbHandle>(fd, window_size, config, mode);

        return std::make_unique<TlbWindow>(std::move(handle), offset);
    }

    std::unique_ptr<TlbWindow> map_tlb_2M(uint32_t x, uint32_t y, uint64_t address, CacheMode mode)
    {
        return map_tlb(x, y, address, mode, 1 << 21);
    }

    void noc_write32(uint32_t x, uint32_t y, uint64_t address, uint32_t value)
    {
        auto window = map_tlb_2M(x, y, address, Uncached);
        window->write32(0, value);
    }

    uint32_t noc_read32(uint32_t x, uint32_t y, uint64_t address)
    {
        auto window = map_tlb_2M(x, y, address, Uncached);
        return window->read32(0);
    }

    uint64_t map_for_dma(void* buffer, size_t size)
    {
        tenstorrent_pin_pages pin{};
        pin.in.output_size_bytes = sizeof(pin.out);
        pin.in.virtual_address = reinterpret_cast<uint64_t>(buffer);
        pin.in.size = size;

        if (ioctl(fd, TENSTORRENT_IOCTL_PIN_PAGES, &pin) != 0) {
            throw std::system_error(errno, std::generic_category(), "Failed to pin pages");
        }

        uint64_t iova = pin.out.physical_address;

        LOG_DEBUG("Mapped buffer at VA %p to IOVA %lx", buffer, iova);

        return iova;
    }

    void unmap_for_dma(void* buffer, size_t size)
    {
        tenstorrent_unpin_pages unpin{};
        unpin.in.virtual_address = reinterpret_cast<uintptr_t>(buffer);
        unpin.in.size = size;

        if (ioctl(fd, TENSTORRENT_IOCTL_UNPIN_PAGES, &unpin) != 0) {
            throw std::system_error(errno, std::generic_category(), "Failed to unpin pages");
        }
    }

    void enable_dbi(bool enable)
    {
        if (is_wormhole()) {
            static constexpr int32_t DBI_REGS_IN_RESET_UNIT = 0x1F30078;
            auto bar4 = wh_map_bar4(fd);
            uint32_t value = enable ? 0x200000 : 0x0;
            bar4.write32(DBI_REGS_IN_RESET_UNIT + 0, value);
            bar4.write32(DBI_REGS_IN_RESET_UNIT + 4, value);
        } else {
            LOG_ERROR("Don't do that");
        }
    }

    ~Device() noexcept
    {
        close(fd);
        LOG_DEBUG("Closed device");
    }
};

// TODO: maybe get rid of this subclassery
class Wormhole : public Device
{
    MappedMemory bar4;  // PCIe TLB registers, NOC2AXI, ARC CSM, Reset Unit.
public:
    Wormhole(const std::string& chardev_path)
        : Device(chardev_path, WORMHOLE_ID)
        , bar4(wh_map_bar4(fd))
    {
        LOG_INFO("Opened a Wormhole device: %s", chardev_path.c_str());
    }

    std::unique_ptr<TlbWindow> map_tlb_1M(uint32_t x, uint32_t y, uint64_t address, CacheMode mode)
    {
        return map_tlb(x, y, address, mode, 1 << 20);
    }

    std::unique_ptr<TlbWindow> map_tlb_16M(uint32_t x, uint32_t y, uint64_t address, CacheMode mode)
    {
        return map_tlb(x, y, address, mode, 1 << 24);
    }

    MappedMemory& get_bar4()
    {
        return bar4;
    }

};

class Blackhole : public Device
{
public:
    Blackhole(const std::string& chardev_path)
        : Device(chardev_path, BLACKHOLE_ID)
    {
        LOG_INFO("Opened a Blackhole device: %s", chardev_path.c_str());
    }

    std::unique_ptr<TlbWindow> map_tlb_4G(uint32_t x, uint32_t y, uint64_t address, CacheMode mode)
    {
        return map_tlb(x, y, address, mode, 1ULL << 32);
    }
};