#pragma once

#include "constants.hpp"
#include "ioctl.h"
#include "logger.hpp"
#include "types.hpp"

#include <cstdint>
#include <fstream>
#include <iomanip>
#include <optional>
#include <sstream>

#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

struct PciDeviceInfo
{
    uint16_t vendor_id;
    uint16_t device_id;
    uint16_t pci_domain;
    uint16_t pci_bus;
    uint16_t pci_device;
    uint16_t pci_function;
};

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

inline uint32_t ioctl_get_driver_version(int fd)
{
    tenstorrent_get_driver_info info{};

    info.in.output_size_bytes = sizeof(tenstorrent_get_driver_info_out);

    if (ioctl(fd, TENSTORRENT_IOCTL_GET_DRIVER_INFO, &info) == -1) {
        SYSTEM_ERROR("Failed to get driver info");
    }

    return info.out.driver_version;
}

inline PciDeviceInfo ioctl_get_device_info(int fd)
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

inline tenstorrent_mapping ioctl_get_mapping(int fd, int id)
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

inline MappedMemory bh_map_noc2axi(int fd)
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

inline MappedMemory map_bar2(int fd)
{
    auto resource = ioctl_get_mapping(fd, TENSTORRENT_MAPPING_RESOURCE1_UC); // BAR2 is index 1
    void* mem = mmap(nullptr, resource.mapping_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, resource.mapping_base);

    if (mem == MAP_FAILED) {
        SYSTEM_ERROR("Failed to map BAR2");
    }

    return MappedMemory{static_cast<uint8_t*>(mem), resource.mapping_size};
}

inline MappedMemory wh_map_bar4(int fd)
{
    auto resource = ioctl_get_mapping(fd, TENSTORRENT_MAPPING_RESOURCE2_WC); // BAR4 is index 2 (BAR2 is index 1)
    void* mem = mmap(nullptr, resource.mapping_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, resource.mapping_base);

    if (mem == MAP_FAILED) {
        SYSTEM_ERROR("Failed to map BAR4");
    }

    return MappedMemory{static_cast<uint8_t*>(mem), resource.mapping_size};
}