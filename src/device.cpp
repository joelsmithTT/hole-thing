// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
// SPDX-License-Identifier: GPL-2.0-only

#include "device.hpp"
#include "types.hpp"
#include "ioctl.h"
#include "logger.hpp"

#include <algorithm>
#include <fstream>
#include <filesystem>
#include <system_error>

namespace {

template <typename T>
static std::optional<T> read_sysfs(const PciDeviceInfo& device_info, const std::string& attribute_name) {
    std::stringstream ss;
    ss << "/sys/bus/pci/devices/"
       << std::hex << std::setfill('0')
       << std::setw(4) << device_info.pci_domain << ":"
       << std::setw(2) << device_info.pci_bus << ":"
       << std::setw(2) << device_info.pci_device << "."
       << std::setw(1) << device_info.pci_function << "/"
       << attribute_name;
    const auto sysfs_path = ss.str();
    std::ifstream attribute_file(sysfs_path);
    std::string value_str;
    T value;

    if (!attribute_file.is_open() || !std::getline(attribute_file, value_str)) {
        return std::nullopt;
    }

    std::istringstream iss(value_str);

    // Handle hexadecimal input for integer types.
    if constexpr (std::is_integral_v<T>) {
        if (value_str.substr(0, 2) == "0x") {
            iss >> std::hex;
        }
    }

    if (!(iss >> value)) {
        return std::nullopt;
    }

    return value;
}

uint32_t ioctl_get_driver_version(int fd)
{
    tenstorrent_get_driver_info info{};

    info.in.output_size_bytes = sizeof(tenstorrent_get_driver_info_out);

    if (ioctl(fd, TENSTORRENT_IOCTL_GET_DRIVER_INFO, &info) == -1) {
        SYSTEM_ERROR("Failed to get driver info");
    }

    return info.out.driver_version;
}

PciDeviceInfo ioctl_get_device_info(int fd)
{
    tenstorrent_get_device_info info{};

    info.in.output_size_bytes = sizeof(tenstorrent_get_device_info_out);

    if (ioctl(fd, TENSTORRENT_IOCTL_GET_DEVICE_INFO, &info) == -1) {
        SYSTEM_ERROR("Failed to get device info");
    }

    uint16_t bus = info.out.bus_dev_fn >> 8;
    uint16_t dev = (info.out.bus_dev_fn >> 3) & 0x1F;
    uint16_t fn = info.out.bus_dev_fn & 0x07;

    return PciDeviceInfo{info.out.vendor_id, info.out.device_id, info.out.pci_domain, bus, dev, fn};
}

tenstorrent_mapping ioctl_get_mapping(int fd, int id)
{
    static const size_t NUM_MAPPINGS = 8; // TODO(jms) magic 8
    struct
    {
        tenstorrent_query_mappings query_mappings{};
        tenstorrent_mapping mapping_array[NUM_MAPPINGS];
    } mappings;

    // HACK
    if (fd < 0) {
        SYSTEM_ERROR("Failed to open device");
    }

    mappings.query_mappings.in.output_mapping_count = NUM_MAPPINGS;

    if (ioctl(fd, TENSTORRENT_IOCTL_QUERY_MAPPINGS, &mappings.query_mappings) == -1) {
        SYSTEM_ERROR("Failed to query mappings");
    }

    for (size_t i = 0; i < NUM_MAPPINGS; i++) {
        if (mappings.mapping_array[i].mapping_id == id) {
            return mappings.mapping_array[i];
        }
    }

    RUNTIME_ERROR("Mapping ID %d not found", id);
}

MappedMemory map_bar2(int fd)
{
    auto resource = ioctl_get_mapping(fd, TENSTORRENT_MAPPING_RESOURCE1_UC); // BAR2 is index 1
    void* mem = mmap(nullptr, resource.mapping_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, resource.mapping_base);

    if (mem == MAP_FAILED) {
        SYSTEM_ERROR("Failed to map BAR2");
    }

    return MappedMemory{static_cast<uint8_t*>(mem), resource.mapping_size};
}

MappedMemory wh_map_bar4(int fd)
{
    auto resource = ioctl_get_mapping(fd, TENSTORRENT_MAPPING_RESOURCE2_WC); // BAR4 is index 2 (BAR2 is index 1)
    void* mem = mmap(nullptr, resource.mapping_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, resource.mapping_base);

    if (mem == MAP_FAILED) {
        SYSTEM_ERROR("Failed to map BAR4");
    }

    return MappedMemory{static_cast<uint8_t*>(mem), resource.mapping_size};
}

MappedMemory map_bar0(int fd)
{
    auto resource = ioctl_get_mapping(fd, TENSTORRENT_MAPPING_RESOURCE0_UC); // BAR0 is index 0
    LOG_INFO("Mapping BAR0 at %lx, size %lx", resource.mapping_base, resource.mapping_size);
    void* mem = mmap(nullptr, resource.mapping_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, resource.mapping_base);

    if (mem == MAP_FAILED) {
        SYSTEM_ERROR("Failed to map BAR0");
    }

    return MappedMemory{static_cast<uint8_t*>(mem), resource.mapping_size};
}

MappedMemory bh_map_noc2axi(int fd)
{
    static constexpr uint64_t NOC2AXI_BASE = 0x1FD00000; // BAR0
    static constexpr uint64_t NOC2AXI_SIZE = 0x00100000; // bytes

    if (ioctl_get_device_info(fd).device_id != BLACKHOLE_ID) {
        RUNTIME_ERROR("Not a Blackhole");
    }

    auto resource = ioctl_get_mapping(fd, TENSTORRENT_MAPPING_RESOURCE0_UC);
    off_t offset = resource.mapping_base + NOC2AXI_BASE;
    void* mem = mmap(nullptr, NOC2AXI_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, offset);

    if (mem == MAP_FAILED) {
        SYSTEM_ERROR("Failed to map NOC2AXI");
    }

    return MappedMemory{static_cast<uint8_t*>(mem), NOC2AXI_SIZE};
}

} // namespace

/* static */ std::vector<std::string> Device::enumerate_devices()
{
    std::vector<std::string> devices;
    const std::string base_path = "/dev/tenstorrent/";

    for (const auto& entry : std::filesystem::directory_iterator(base_path)) {
        if (std::filesystem::is_character_file(entry.path()) || std::filesystem::is_block_file(entry.path())) {
            devices.push_back(entry.path().string());
        }
    }

    std::sort(devices.begin(), devices.end());
    return devices;
}

Device::Device(const std::string& chardev_path)
    : fd(open(chardev_path.c_str(), O_RDWR | O_CLOEXEC))
    , bar2(map_bar2(fd))
    , device_info(ioctl_get_device_info(fd))
{
    static const std::string KMD_VERSION_PATH = "/sys/module/tenstorrent/version";
    const char* device = is_wormhole() ? "Wormhole" : is_blackhole() ? "Blackhole" : "UNKNOWN";
    const char* iommu = iommu_enabled() ? "enabled" : "disabled";
    const auto kmd_version = read_small_file<std::string>(KMD_VERSION_PATH);
    const char* kmd = kmd_version.value().c_str();
    uint32_t driver_version = ioctl_get_driver_version(fd);

    LOG_INFO("Opened %s at %04x:%02x:%02x.%x with IOMMU %s, KMD %s (v%u)", device, device_info.pci_domain,
             device_info.pci_bus, device_info.pci_device, device_info.pci_function, iommu, kmd, driver_version);
}

bool Device::iommu_enabled() const
{
    auto iommu_type = read_sysfs<std::string>(device_info, "iommu_group/type");
    if (iommu_type) {
        return iommu_type->substr(0, 3) == "DMA"; // DMA or DMA-FQ
    }
    return false;
}

int Device::get_fd() const
{
    return fd;
}

bool Device::is_wormhole() const
{
    return device_info.device_id == WORMHOLE_ID;
}

bool Device::is_blackhole() const
{
    return device_info.device_id == BLACKHOLE_ID;
}

PciDeviceInfo Device::get_device_info() const
{
    return device_info;
}

MappedMemory& Device::get_bar2()
{
    return bar2;
}

MappedMemory Device::get_bar4()
{
    if (!is_wormhole()) {
        LOG_FATAL("Don't do that");
    }

    return wh_map_bar4(fd);
}

coord_t Device::get_pcie_coordinates()
{
    if (is_wormhole()) {
        return {0, 3};
    } else if (is_blackhole()) {
        static constexpr uint64_t NOC_ID_OFFSET = 0x4044;

        auto noc2axi = bh_map_noc2axi(fd);
        auto noc_id = noc2axi.read32(NOC_ID_OFFSET);
        auto x = (noc_id >> 0x0) & 0x3f;
        auto y = (noc_id >> 0x6) & 0x3f;

        LOG_INFO("Blackhole: host-visible PCIe core is at NOC0 coordinates (x=%u, y=%u)", x, y);

        return {x, y};
    } else {
        LOG_FATAL("Unknown device type");
    }
    return {0, 0};
}

coord_t Device::get_noc_grid_size() const
{
    if (is_wormhole()) {
        return {10, 12};
    } else if (is_blackhole()) {
        return {17, 12};
    }
    LOG_FATAL("Unknown device type");
    return {0, 0};
}

std::unique_ptr<TlbWindow> Device::map_tlb(uint16_t x, uint16_t y, uint64_t address, CacheMode mode, size_t size, int noc)
{
    const uint64_t window_mask = size - 1;
    const uint64_t addr = address & ~window_mask;
    const uint64_t offset = address & window_mask;
    const tenstorrent_noc_tlb_config config{
        .addr = addr,
        .x_end = x,
        .y_end = y,
    };

    // LOG_DEBUG("Mapping TLB window: x=%u, y=%u, address=0x%lx, offset=0x%lx, mode=%d", x, y, addr, offset, mode);
    auto handle = std::make_unique<TlbHandle>(fd, size, config, mode);

    return std::make_unique<TlbWindow>(std::move(handle), offset);
}

std::unique_ptr<TlbWindow> Device::map_tlb_2M(uint16_t x, uint16_t y, uint64_t address, CacheMode mode, int noc)
{
    return map_tlb(x, y, address, mode, 1 << 21, noc);
}

std::unique_ptr<TlbWindow> Device::map_tlb_4G(uint16_t x, uint16_t y, uint64_t address, CacheMode mode, int noc)
{
    if (!is_blackhole()) {
        LOG_FATAL("No 4GB TLB windows on Wormhole");
    }
    return map_tlb(x, y, address, mode, 1ULL << 32, noc);
}

void Device::write_block(uint16_t x, uint16_t y, uint64_t address, const void* src, size_t size, int noc)
{
    constexpr size_t window_size = 1 << 21; // 2MB window size
    constexpr uint64_t window_mask = window_size - 1;
    uint64_t current_addr = address;
    size_t remaining = size;
    const uint8_t* data = static_cast<const uint8_t*>(src);

    while (remaining > 0) {
        // Calculate window base address and offset
        uint64_t window_base = current_addr & ~window_mask;
        uint64_t window_offset = current_addr & window_mask;

        // Calculate how much we can write in this window
        size_t write_size = std::min(remaining, window_size - window_offset);

        // Map TLB window for this range
        auto window = map_tlb_2M(x, y, window_base, WriteCombined, noc);

        // Write data in 4-byte chunks
        for (size_t i = 0; i < write_size; i += 4) {
            if (i + 4 <= write_size) {
                window->write32(window_offset + i, *reinterpret_cast<const uint32_t*>(data + i));
            }
        }

        // Update for next iteration
        current_addr += write_size;
        data += write_size;
        remaining -= write_size;
    }
}

void Device::noc_write32(uint16_t x, uint16_t y, uint64_t address, uint32_t value, int noc)
{
    auto window = map_tlb_2M(x, y, address, Uncached, noc);
    window->write32(0, value);
}

uint32_t Device::noc_read32(uint16_t x, uint16_t y, uint64_t address, int noc)
{
    auto window = map_tlb_2M(x, y, address, Uncached, noc);
    return window->read32(0);
}

uint64_t Device::map_for_dma(void* buffer, size_t size)
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

void Device::unmap_for_dma(void* buffer, size_t size)
{
    tenstorrent_unpin_pages unpin{};
    unpin.in.virtual_address = reinterpret_cast<uintptr_t>(buffer);
    unpin.in.size = size;

    if (ioctl(fd, TENSTORRENT_IOCTL_UNPIN_PAGES, &unpin) != 0) {
        throw std::system_error(errno, std::generic_category(), "Failed to unpin pages");
    }
}

void Device::enable_dbi(bool enable)
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

Device::~Device() noexcept
{
    close(fd);
    LOG_DEBUG("Closed device");
}
