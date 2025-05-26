#pragma once

#include "ttio.h"

#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <utility>

#define OK(x) \
do { \
    int ret = (x); \
    if (ret != 0) { \
        fprintf(stderr, "Error: %s: %d\n", #x, ret); \
        exit(1); \
    } \
} while (0)

namespace tt {

class Device
{
    tt_device_t* device;
    uint32_t driver_version;
    uint16_t vendor_id;
    uint16_t device_id;
    uint16_t pci_domain;
    uint16_t pci_bus;
    uint16_t pci_device;
    uint16_t pci_function;

public:
    Device(const char* chardev_path)
    {
        OK(tt_device_open(chardev_path, &device));
        OK(tt_driver_get_attr(device, TT_DRIVER_ATTR_VERSION, &driver_version));
        OK(tt_device_get_attr(device, TT_DEVICE_ATTR_PCI_VENDOR_ID, &vendor_id));
        OK(tt_device_get_attr(device, TT_DEVICE_ATTR_PCI_DEVICE_ID, &device_id));
        OK(tt_device_get_attr(device, TT_DEVICE_ATTR_PCI_DOMAIN, &pci_domain));
        OK(tt_device_get_attr(device, TT_DEVICE_ATTR_PCI_BUS, &pci_bus));
        OK(tt_device_get_attr(device, TT_DEVICE_ATTR_PCI_DEVICE, &pci_device));
        OK(tt_device_get_attr(device, TT_DEVICE_ATTR_PCI_FUNCTION, &pci_function));

        printf("Device: %04x:%04x; driver: %d\n", vendor_id, device_id, driver_version);
        printf("PCI: %04x:%02x:%02x.%x\n", pci_domain, pci_bus, pci_device, pci_function);
    }

    bool is_wormhole() const
    {
        return device_id == WORMHOLE_PCI_DEVICE_ID;
    }

    bool is_blackhole() const
    {
        return device_id == BLACKHOLE_PCI_DEVICE_ID;
    }

    using iova_t = uint64_t;
    using noc_addr_t = uint64_t;
    std::pair<iova_t, noc_addr_t> map_for_dma(void* addr, size_t size)
    {
        uint64_t iova, noc_addr;
        OK(tt_dma_map(device, addr, size, &iova, &noc_addr));
        return {iova, noc_addr};
    }

    void unmap_for_dma(void* addr, size_t size)
    {
        OK(tt_dma_unmap(device, addr, size));
    }

    uint32_t noc_read32(uint16_t x, uint16_t y, uint64_t addr)
    {
        uint32_t value;
        OK(tt_noc_read32(device, x, y, addr, &value));
        return value;
    }
    
    void noc_write32(uint16_t x, uint16_t y, uint64_t addr, uint32_t value)
    {
        OK(tt_noc_write32(device, x, y, addr, value));
    }

    void noc_write_mem(uint16_t x, uint16_t y, uint64_t addr, const void* src, size_t size)
    {
        OK(tt_noc_write(device, x, y, addr, src, size));
    }

    void noc_read_mem(uint16_t x, uint16_t y, uint64_t addr, void* dst, size_t size)
    {
        OK(tt_noc_read(device, x, y, addr, dst, size));
    }

    ~Device()
    {
        tt_device_close(device);
    }
};

} // namespace tt
