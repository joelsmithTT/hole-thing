#ifndef TTIO_H
#define TTIO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TENSTORRENT_PCI_VENDOR_ID 0x1e52
#define BLACKHOLE_PCI_DEVICE_ID 0xb140
#define WORMHOLE_PCI_DEVICE_ID 0x401e

typedef struct tt_device_t tt_device_t;         /* Handle to a Tenstorrent PCIe device */
typedef struct tt_tlb_t tt_tlb_t;               /* Handle to a TLB - MMIO mapping to NOC endpoint(s) */
typedef struct tt_noc_params_t tt_noc_params_t; /* Describes NOC endpoint & access parameters */

/* Types */
enum tt_device_attr {
    TT_DEVICE_ATTR_PCI_DOMAIN = 0,
    TT_DEVICE_ATTR_PCI_BUS = 1,
    TT_DEVICE_ATTR_PCI_DEVICE = 2,
    TT_DEVICE_ATTR_PCI_FUNCTION = 3,
    TT_DEVICE_ATTR_PCI_VENDOR_ID = 4,
    TT_DEVICE_ATTR_PCI_DEVICE_ID = 5,
    TT_DEVICE_ATTR_PCI_SUBSYSTEM_ID = 6,
    TT_DEVICE_ATTR_NOC_TRANSLATION_EN = 7,
};

enum tt_driver_attr {
    TT_DRIVER_ATTR_VERSION = 0,
};

enum tt_noc_ordering {
    TT_NOC_ORDERING_RELAXED         = 0,
    TT_NOC_ORDERING_STRICT          = 1,
    TT_NOC_ORDERING_POSTED          = 2,
    TT_NOC_ORDERING_POSTED_STRICT   = 3     /* BH only, Unicast only */
};

enum tt_mmio_cache_mode {
    TT_MMIO_CACHE_MODE_UC = 0, /* Uncached */
    TT_MMIO_CACHE_MODE_WC = 1, /* Write Combined */
};

/* Open/close a device */
int32_t tt_device_open(const char* chardev_path, tt_device_t** out_device);
int32_t tt_device_close(tt_device_t* device);

/* Get device/driver attributes */
int32_t tt_device_get_attr(tt_device_t* device, uint32_t attr, void* out_value);
int32_t tt_driver_get_attr(tt_device_t* device, uint32_t attr, void* out_value);

/* Map/unmap a region of memory for DMA; address & size must be page aligned */
/* This is the primary interface for device access to host memory */
int32_t tt_dma_map(tt_device_t* device, void* addr, size_t size, uint64_t* out_iova, uint64_t* out_noc_addr);
int32_t tt_dma_unmap(tt_device_t* device, void* addr, size_t size);

/* TLB interface - MMIO mapping to device NOC */
/* This is the primary interface for host access to device address spaces */
int32_t tt_tlb_alloc(tt_device_t* device, size_t size, enum tt_mmio_cache_mode mode, tt_tlb_t** out_tlb);
int32_t tt_tlb_free(tt_tlb_t* tlb);
int32_t tt_tlb_get_mmio(tt_tlb_t* tlb, void** out_mmio);
int32_t tt_tlb_set(tt_tlb_t* tlb, uint16_t x, uint16_t y, uint64_t addr);
int32_t tt_tlb_set_params(tt_tlb_t* tlb, tt_noc_params_t* params);

/* Convenience access chip registers via PCIe->NOC MMIO (inefficient) */
int32_t tt_noc_read32(tt_device_t* device, uint16_t x, uint16_t y, uint64_t addr, uint32_t* value);
int32_t tt_noc_write32(tt_device_t* device, uint16_t x, uint16_t y, uint64_t addr, uint32_t value);

/* Convenience access chip memory (e.g. L1, DRAM) via PCIe->NOC MMIO (inefficient) */
int32_t tt_noc_read(tt_device_t* device, uint16_t x, uint16_t y, uint64_t addr, void* dst, size_t size);
int32_t tt_noc_write(tt_device_t* device, uint16_t x, uint16_t y, uint64_t addr, const void* src, size_t size);

/* NOC params interface - describe NOC endpoint & access parameters */
int32_t tt_noc_params_alloc(tt_noc_params_t** out_params);
int32_t tt_noc_params_set_xy_end(tt_noc_params_t* params, int16_t x, int16_t y); /* Unicast */
int32_t tt_noc_params_set_xy_start(tt_noc_params_t* params, int16_t x, int16_t y);
int32_t tt_noc_params_set_noc(tt_noc_params_t* params, uint8_t noc);
int32_t tt_noc_params_set_mcast(tt_noc_params_t* params, bool mcast);
int32_t tt_noc_params_set_ordering(tt_noc_params_t* params, enum tt_noc_ordering ordering);
int32_t tt_noc_params_set_linked(tt_noc_params_t* params, bool linked);
int32_t tt_noc_params_set_static_vc(tt_noc_params_t* params, uint8_t static_vc);
int32_t tt_noc_params_free(tt_noc_params_t* params);

#ifdef __cplusplus
}
#endif

#endif // TTIO_H