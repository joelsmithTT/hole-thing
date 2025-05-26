#ifndef TTIO_H
#define TTIO_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TENSTORRENT_PCI_VENDOR_ID 0x1e52
#define BLACKHOLE_PCI_DEVICE_ID 0xb140
#define WORMHOLE_PCI_DEVICE_ID 0x401e

typedef struct tt_device_t tt_device_t; /* Handle to a Tenstorrent PCIe device */
typedef struct tt_tlb_t tt_tlb_t; /* Handle to a TLB - MMIO mapping to NOC endpoint(s) */
typedef struct tt_noc_target_t tt_noc_target_t; /* Describes NOC endpoint & access parameters */

/* Open and close a device */
int32_t tt_device_open(const char* chardev_path, tt_device_t** out_device);
int32_t tt_device_close(tt_device_t* device);

/* Get device attributes */
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

int32_t tt_device_get_attr(tt_device_t* device, uint32_t attr, void* out_value);

/* Get driver attributes */
enum tt_driver_attr {
    TT_DRIVER_ATTR_VERSION = 0,
};

int32_t tt_driver_get_attr(tt_device_t* device, uint32_t attr, void* out_value);

/* Map and unmap a region of memory, addres & size must be page aligned */
int32_t tt_dma_map(tt_device_t* device, void* addr, size_t size, uint64_t* out_iova, uint64_t* out_noc_addr);
int32_t tt_dma_unmap(tt_device_t* device, void* addr, size_t size);

/* Allocate a kernel-managed buffer, you shouldn't use this */
int32_t tt_dma_alloc(tt_device_t* device, size_t size, void** out_buf, uint64_t* out_iova, uint64_t* out_noc_addr);
int32_t tt_dma_free(tt_device_t* device, void* buf, size_t size);

/* Access chip registers via PCIe->NOC MMIO */
int32_t tt_noc_read32(tt_device_t* device, uint16_t x, uint16_t y, uint64_t addr, uint32_t* value);
int32_t tt_noc_write32(tt_device_t* device, uint16_t x, uint16_t y, uint64_t addr, uint32_t value);

/* Access chip memory regions (e.g. L1, DRAM) via PCIe->NOC MMIO */
int32_t tt_noc_read(tt_device_t* device, uint16_t x, uint16_t y, uint64_t addr, void* dst, size_t size);
int32_t tt_noc_write(tt_device_t* device, uint16_t x, uint16_t y, uint64_t addr, const void* src, size_t size);

/* TLB interface - get your own MMIO mapping to NOC endpoint(s) */
int32_t tt_tlb_alloc(tt_device_t* device, size_t size, tt_tlb_t** out_tlb);
int32_t tt_tlb_config(tt_tlb_t* tlb, uint16_t x, uint16_t y, uint64_t addr);    /* Simple config */
int32_t tt_tlb_config_target(tt_tlb_t* tlb, tt_noc_target_t* target);           /* Full target config */
int32_t tt_tlb_mmap_uc(tt_tlb_t* tlb, void** out_mapping);                      /* Map Uncached */
int32_t tt_tlb_mmap_wc(tt_tlb_t* tlb, void** out_mapping);                      /* Map Write Combined */
int32_t tt_tlb_munmap(tt_tlb_t* tlb, void* mapping);
int32_t tt_tlb_free(tt_tlb_t* tlb);

/* NOC target interface - describe NOC endpoint & access parameters */
int32_t tt_noc_target_alloc(tt_noc_target_t** out_params);
int32_t tt_noc_target_set_xy_end(tt_noc_target_t* params, int16_t x, int16_t y);
int32_t tt_noc_target_set_xy_start(tt_noc_target_t* params, int16_t x, int16_t y);
int32_t tt_noc_target_set_noc(tt_noc_target_t* params, uint8_t noc);
int32_t tt_noc_target_set_mcast(tt_noc_target_t* params, uint8_t mcast);
int32_t tt_noc_target_set_ordering(tt_noc_target_t* params, uint8_t ordering);
int32_t tt_noc_target_set_linked(tt_noc_target_t* params, uint8_t linked);
int32_t tt_noc_target_set_static_vc(tt_noc_target_t* params, uint8_t static_vc);
int32_t tt_noc_target_free(tt_noc_target_t* params);

#ifdef __cplusplus
}
#endif

#endif // TTIO_H