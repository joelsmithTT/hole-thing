#include "ttio.h"
#include "ioctl.h"

#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdbool.h>

#define UNUSED(x) (void)(x)
#define TWO_MEGABYTES (1UL << 21)
#define MIN(a, b) ((a) < (b) ? (a) : (b))

struct tt_device_t
{
    int fd;
    pthread_mutex_t dmabufs_mutex;
    int32_t dmabufs[TENSTORRENT_MAX_DMA_BUFS];
};

struct tt_tlb_t
{
    struct tt_device_t* device;
    int32_t id;
    size_t size;
    uint64_t mmap_offset_uc;
    uint64_t mmap_offset_wc;
    void* active_mapping;
};

struct tt_noc_target_t
{
    uint64_t addr;
	uint16_t x_end;
	uint16_t y_end;
	uint16_t x_start;
	uint16_t y_start;
	uint8_t noc;
	uint8_t mcast;
	uint8_t ordering;
	uint8_t linked;
	uint8_t static_vc;
};

// Prototypes
int32_t tt_noc_read32(tt_device_t* device, uint16_t x, uint16_t y, uint64_t addr, uint32_t* value);
int32_t tt_noc_write32(tt_device_t* device, uint16_t x, uint16_t y, uint64_t addr, uint32_t value);
int32_t tt_noc_read(tt_device_t* device, uint16_t x, uint16_t y, uint64_t addr, void* dst, size_t size);
int32_t tt_noc_write(tt_device_t* device, uint16_t x, uint16_t y, uint64_t addr, const void* src, size_t size);

int32_t tt_device_open(const char* chardev_path, tt_device_t** out_device)
{
    struct tt_device_t* dev = (struct tt_device_t*)malloc(sizeof(struct tt_device_t));

    if (!dev) {
        return -ENOMEM;
    }

    memset(dev, 0, sizeof(struct tt_device_t));

    dev->fd = open(chardev_path, O_RDWR | O_CLOEXEC);
    if (dev->fd == -1) {
        free(dev);
        return -errno;
    }

    pthread_mutex_init(&dev->dmabufs_mutex, NULL);

    *out_device = dev;

    return 0;
}

int32_t tt_device_close(tt_device_t* device)
{
    int ret = 0;

    pthread_mutex_destroy(&device->dmabufs_mutex);

    if (close(device->fd) != 0) {
        ret = -errno;
    }

    free(device);
    return ret;
}

#define WH_NIU_CFG_BASE 0x1000A0000
#define WH_NIU_CFG_OFFSET 0x100
#define BH_NIU_CFG 0x4100

static int32_t get_noc_translation_en(tt_device_t* device, uint16_t device_id, bool* out_value)
{
    *out_value = true;
    return 0;
#if 0
    if (device_id == WORMHOLE_PCI_DEVICE_ID) {
        uint32_t value;
        tt_noc_read32(device, 0, 0, WH_NIU_CFG_BASE + WH_NIU_CFG_OFFSET, &value);
        *out_value = (value >> 14) & 0x1;
    } else if (device_id == BLACKHOLE_PCI_DEVICE_ID) {
        // TOOD: map BAR0 NOC2AXI...
        *out_value = true;
    }
#endif
    return 0;
}

int32_t tt_device_get_attr(tt_device_t* device, uint32_t attr, void* out_value)
{
    struct tenstorrent_get_device_info get_device_info = {0};

    get_device_info.in.output_size_bytes = sizeof(get_device_info.out);

    if (ioctl(device->fd, TENSTORRENT_IOCTL_GET_DEVICE_INFO, &get_device_info) != 0) {
        return -errno;
    }

    switch (attr) {
        case TT_DEVICE_ATTR_PCI_DOMAIN:
            *((uint16_t*)out_value) = get_device_info.out.pci_domain;
            break;
        case TT_DEVICE_ATTR_PCI_BUS:
            *((uint16_t*)out_value) = get_device_info.out.bus_dev_fn >> 8;
            break;
        case TT_DEVICE_ATTR_PCI_DEVICE:
            *((uint16_t*)out_value) = (get_device_info.out.bus_dev_fn >> 3) & 0x1F;
            break;
        case TT_DEVICE_ATTR_PCI_FUNCTION:
            *((uint16_t*)out_value) = get_device_info.out.bus_dev_fn & 0x07;
            break;
        case TT_DEVICE_ATTR_PCI_VENDOR_ID:
            *((uint16_t*)out_value) = get_device_info.out.vendor_id;
            break;
        case TT_DEVICE_ATTR_PCI_DEVICE_ID:
            *((uint16_t*)out_value) = get_device_info.out.device_id;
            break;
        case TT_DEVICE_ATTR_NOC_TRANSLATION_EN:
            return get_noc_translation_en(device, get_device_info.out.device_id, (bool*)out_value);
        default:
            return -EINVAL;
    }

    return 0;
}

int32_t tt_driver_get_attr(tt_device_t* device, uint32_t attr, void* out_value)
{
    struct tenstorrent_get_driver_info get_driver_info = {0};

    if (!device) return -EINVAL;

    get_driver_info.in.output_size_bytes = sizeof(get_driver_info.out);

    if (ioctl(device->fd, TENSTORRENT_IOCTL_GET_DRIVER_INFO, &get_driver_info) != 0) {
        return -errno;
    }

    switch (attr) {
        case TT_DRIVER_ATTR_VERSION:
            *((uint32_t*)out_value) = get_driver_info.out.driver_version;
            break;
        default:
            return -EINVAL;
    }

    return 0;
}

int32_t tt_dma_map(tt_device_t* device, void* addr, size_t size, uint64_t* out_iova, uint64_t* out_noc_addr)
{
    struct {
        struct tenstorrent_pin_pages_in in;
        struct tenstorrent_pin_pages_out_extended out;
    } pin = {0};

    if (!addr) return -EINVAL;
    if (size == 0) return -EINVAL;
    if (size % 4096 != 0) return -EINVAL;
    if (!out_iova) return -EINVAL;
    if (!out_noc_addr) return -EINVAL;

    pin.in.output_size_bytes = sizeof(pin.out);
    pin.in.virtual_address = (uint64_t)addr;
    pin.in.size = size;
    pin.in.flags = TENSTORRENT_PIN_PAGES_NOC_DMA;

    if (ioctl(device->fd, TENSTORRENT_IOCTL_PIN_PAGES, &pin) != 0) {
        return -errno;
    }

    *out_noc_addr = pin.out.noc_address;
    *out_iova = pin.out.physical_address;

    return 0;
}

int32_t tt_dma_unmap(tt_device_t* device, void* addr, size_t size)
{
    struct tenstorrent_unpin_pages unpin = {0};

    unpin.in.virtual_address = (uintptr_t)addr;
    unpin.in.size = size;

    if (ioctl(device->fd, TENSTORRENT_IOCTL_UNPIN_PAGES, &unpin) != 0) {
        return -errno;
    }

    return 0;
}

static int32_t find_free_dmabuf(tt_device_t* device)
{
    for (int32_t i = 0; i < TENSTORRENT_MAX_DMA_BUFS; i++) {
        if (device->dmabufs[i] == 0) {
            return i;
        }
    }

    return -1;
}

int32_t tt_dma_alloc(tt_device_t* device, size_t size, void** out_buf, uint64_t* out_iova, uint64_t* out_noc_addr)
{
    struct tenstorrent_allocate_dma_buf dmabuf = {0};
    int32_t buf_index;

    if (!out_buf) return -EINVAL;
    if (!out_iova) return -EINVAL;
    if (!out_noc_addr) return -EINVAL;
    if (size == 0) return -EINVAL;
    if (size > (1 << 28)) return -EINVAL;   // TODO: ask the driver at open time
    if (size % 4096 != 0) return -EINVAL;

    pthread_mutex_lock(&device->dmabufs_mutex);
    buf_index = find_free_dmabuf(device);

    if (buf_index == -1) {
        pthread_mutex_unlock(&device->dmabufs_mutex);
        return -ENOMEM;
    }
    device->dmabufs[buf_index] = 1;

    dmabuf.in.requested_size = size;
    dmabuf.in.flags = TENSTORRENT_ALLOCATE_DMA_BUF_NOC_DMA;
    dmabuf.in.buf_index = (uint8_t)buf_index;
    
    if (ioctl(device->fd, TENSTORRENT_IOCTL_ALLOCATE_DMA_BUF, &dmabuf) != 0) {
        device->dmabufs[buf_index] = 0;
        pthread_mutex_unlock(&device->dmabufs_mutex);
        return -errno;
    }

    pthread_mutex_unlock(&device->dmabufs_mutex);

    *out_iova = dmabuf.out.physical_address;
    *out_noc_addr = dmabuf.out.noc_address;
    *out_buf = mmap(NULL, dmabuf.out.size, PROT_READ | PROT_WRITE, MAP_SHARED, device->fd, dmabuf.out.mapping_offset);

    if (*out_buf == MAP_FAILED) {
        return -errno;
    }

    return 0;
}

int32_t tt_dma_free(tt_device_t* device, void* buf, size_t size)
{
    munmap(buf, size);
    return -ENOSYS; // TODO: tt-kmd doesn't really support this; you must close the fd.
    UNUSED(device);
}

// TODO: This is stupid, instead we should:
// 1. Get the driver to do it (e.g. TENSTORRENT_IOCTL_NOC_ACCESS, not implemented).
// 2. Allocate and map some TLB windows during tt_device_open:
//    - keep one or more in the tt_device_t
//    - grab one for exclusive access during tt_noc_read32, tt_noc_write32, tt_noc_read, tt_noc_write
//    - put it back when done
int32_t tt_noc_read32(tt_device_t* device, uint16_t x, uint16_t y, uint64_t addr, uint32_t* value)
{
    struct tt_noc_target_t target = {0};
    tt_tlb_t* tlb;
    void* mapping;
    uint64_t offset;
    uint64_t aligned_addr;
    int32_t ret;

    if (!value) return -EINVAL;
    if (addr % 4 != 0) return -EINVAL;

    ret = tt_tlb_alloc(device, TWO_MEGABYTES, &tlb);
    if (ret != 0) {
        return ret;
    }

    ret = tt_tlb_mmap_uc(tlb, &mapping);
    if (ret != 0) {
        tt_tlb_free(tlb);
        return ret;
    }

    aligned_addr = addr & ~(tlb->size - 1);
    offset = addr & (tlb->size - 1);

    target.addr = aligned_addr;
    target.x_end = x;
    target.y_end = y;
    target.noc = 0;
    target.ordering = 1;    // strict

    ret = tt_tlb_config_target(tlb, &target);

    if (ret != 0) {
        tt_tlb_free(tlb);
        return ret;
    }

    *value = *(volatile uint32_t*)((uint8_t*)mapping + offset);

    tt_tlb_free(tlb);
    return 0;
}

int32_t tt_noc_write32(tt_device_t* device, uint16_t x, uint16_t y, uint64_t addr, uint32_t value)
{
    struct tt_noc_target_t target = {0};
    tt_tlb_t* tlb;
    void* mapping;
    uint64_t offset;
    uint64_t aligned_addr;
    int32_t ret;

    if (addr % 4 != 0) return -EINVAL;

    ret = tt_tlb_alloc(device, TWO_MEGABYTES, &tlb);
    if (ret != 0) {
        return ret;
    }

    ret = tt_tlb_mmap_uc(tlb, &mapping);
    if (ret != 0) {
        tt_tlb_free(tlb);
        return ret;
    }

    aligned_addr = addr & ~(tlb->size - 1);
    offset = addr & (tlb->size - 1);

    target.addr = aligned_addr;
    target.x_end = x;
    target.y_end = y;
    target.noc = 0;
    target.ordering = 1;    // strict
    ret = tt_tlb_config_target(tlb, &target);

    if (ret != 0) {
        tt_tlb_free(tlb);
        return ret;
    }

    *(volatile uint32_t*)((uint8_t*)mapping + offset) = value;

    tt_tlb_free(tlb);
    return 0;
}

int32_t tt_noc_read(tt_device_t* device, uint16_t x, uint16_t y, uint64_t addr, void* dst, size_t size)
{
    struct tt_noc_target_t target = {0};
    tt_tlb_t* tlb;
    void* mapping;
    uint8_t* dst_ptr;
    int32_t ret;

    if (addr % 4 != 0) return -EINVAL;
    if (size % 4 != 0) return -EINVAL;

    ret = tt_tlb_alloc(device, TWO_MEGABYTES, &tlb);
    if (ret != 0) {
        return ret;
    }

    ret = tt_tlb_mmap_wc(tlb, &mapping);
    if (ret != 0) {
        tt_tlb_free(tlb);
        return ret;
    }

    target.x_end = x;
    target.y_end = y;
    target.noc = 0;
    dst_ptr = (uint8_t*)dst;

    while (size > 0) {
        uint64_t aligned_addr = addr & ~(tlb->size - 1);
        uint64_t offset = addr & (tlb->size - 1);
        size_t chunk_size = MIN(size, tlb->size - offset);
        uint8_t* src_ptr = (uint8_t*)mapping + offset;

        target.addr = aligned_addr;
        ret = tt_tlb_config_target(tlb, &target);

        if (ret != 0) {
            tt_tlb_free(tlb);
            return ret;
        }

        // TODO
        memcpy(dst_ptr, src_ptr, chunk_size);

        dst_ptr += chunk_size;
        size -= chunk_size;
        addr += chunk_size;
    }

    tt_tlb_free(tlb);
    return 0;
}

int32_t tt_noc_write(tt_device_t* device, uint16_t x, uint16_t y, uint64_t addr, const void* src, size_t size)
{
    struct tt_noc_target_t target = {0};
    tt_tlb_t* tlb;
    void* mapping;
    uint8_t* src_ptr;
    int32_t ret;

    if (addr % 4 != 0) return -EINVAL;
    if (size % 4 != 0) return -EINVAL;

    ret = tt_tlb_alloc(device, TWO_MEGABYTES, &tlb);
    if (ret != 0) {
        return ret;
    }

    ret = tt_tlb_mmap_wc(tlb, &mapping);
    if (ret != 0) {
        tt_tlb_free(tlb);
        return ret;
    }

    target.x_end = x;
    target.y_end = y;
    target.noc = 0;
    src_ptr = (uint8_t*)src;

    while (size > 0) {
        uint64_t aligned_addr = addr & ~(tlb->size - 1);
        uint64_t offset = addr & (tlb->size - 1);
        size_t chunk_size = MIN(size, tlb->size - offset);
        uint8_t* dst_ptr = (uint8_t*)mapping + offset;

        target.addr = aligned_addr;
        ret = tt_tlb_config_target(tlb, &target);

        if (ret != 0) {
            tt_tlb_free(tlb);
            return ret;
        }

        // TODO
        memcpy(dst_ptr, src_ptr, chunk_size);

        src_ptr += chunk_size;
        size -= chunk_size;
        addr += chunk_size;
    }

    tt_tlb_free(tlb);
    return 0;
}


int32_t tt_tlb_alloc(tt_device_t* device, size_t size, tt_tlb_t** out_tlb)
{
    struct tenstorrent_allocate_tlb allocate_tlb = {0};
    struct tt_tlb_t* tlb;

    tlb = (struct tt_tlb_t*)malloc(sizeof(struct tt_tlb_t));
    if (!tlb) {
        return -ENOMEM;
    }

    allocate_tlb.in.size = size;

    if (ioctl(device->fd, TENSTORRENT_IOCTL_ALLOCATE_TLB, &allocate_tlb) != 0) {
        free(tlb);
        return -errno;
    }

    tlb->id = allocate_tlb.out.id;
    tlb->mmap_offset_uc = allocate_tlb.out.mmap_offset_uc;
    tlb->mmap_offset_wc = allocate_tlb.out.mmap_offset_wc;
    tlb->device = device;
    tlb->size = size;
    tlb->active_mapping = NULL;

    *out_tlb = tlb;

    return 0;
}

int32_t tt_tlb_config(tt_tlb_t* tlb, uint16_t x, uint16_t y, uint64_t addr)
{
    struct tenstorrent_configure_tlb configure_tlb = {0};
    struct tt_device_t* device;

    if (!tlb) return -EINVAL;

    device = tlb->device;

    configure_tlb.in.id = tlb->id;
    configure_tlb.in.config.addr = addr;
    configure_tlb.in.config.x_end = x;
    configure_tlb.in.config.y_end = y;

    if (ioctl(device->fd, TENSTORRENT_IOCTL_CONFIGURE_TLB, &configure_tlb) != 0) {
        return -errno;
    }

    return 0;
}

int32_t tt_tlb_config_target(tt_tlb_t* tlb, tt_noc_target_t* target)
{
    struct tenstorrent_configure_tlb configure_tlb = {0};
    struct tt_device_t* device;

    if (!tlb) return -EINVAL;

    device = tlb->device;

    configure_tlb.in.id = tlb->id;
    configure_tlb.in.config.addr = target->addr;
    configure_tlb.in.config.x_end = target->x_end;
    configure_tlb.in.config.y_end = target->y_end;
    configure_tlb.in.config.x_start = target->x_start;
    configure_tlb.in.config.y_start = target->y_start;
    configure_tlb.in.config.noc = target->noc;
    configure_tlb.in.config.mcast = target->mcast;
    configure_tlb.in.config.ordering = target->ordering;
    configure_tlb.in.config.linked = target->linked;
    configure_tlb.in.config.static_vc = target->static_vc;

    if (ioctl(device->fd, TENSTORRENT_IOCTL_CONFIGURE_TLB, &configure_tlb) != 0) {
        return -errno;
    }

    return 0;
}

int32_t tt_tlb_mmap_uc(tt_tlb_t* tlb, void** out_mapping)
{
    struct tt_device_t* device;
    void* mapping;

    if (!tlb) return -EINVAL;
    if (!out_mapping) return -EINVAL;
    if (tlb->active_mapping) return -EINVAL;

    device = tlb->device;

    mapping = mmap(NULL, tlb->size, PROT_READ | PROT_WRITE, MAP_SHARED, device->fd, tlb->mmap_offset_uc);
    if (mapping == MAP_FAILED) {
        return -errno;
    }

    tlb->active_mapping = mapping;
    *out_mapping = mapping;

    return 0;
}

int32_t tt_tlb_mmap_wc(tt_tlb_t* tlb, void** out_mapping)
{
    struct tt_device_t* device;
    void *mapping;

    if (!tlb) return -EINVAL;
    if (!out_mapping) return -EINVAL;
    if (tlb->active_mapping) return -EINVAL;

    device = tlb->device;
    
    mapping = mmap(NULL, tlb->size, PROT_READ | PROT_WRITE, MAP_SHARED, device->fd, tlb->mmap_offset_wc);
    if (mapping == MAP_FAILED) {
        return -errno;
    }

    tlb->active_mapping = mapping;
    *out_mapping = mapping;

    return 0;
}

int32_t tt_tlb_free(tt_tlb_t* tlb)
{
    struct tenstorrent_free_tlb free_tlb = {0};
    struct tt_device_t* device;

    if (!tlb) return -EINVAL;

    device = tlb->device;

    if (tlb->active_mapping) {
        munmap(tlb->active_mapping, tlb->size);
        tlb->active_mapping = NULL;
    }

    free_tlb.in.id = tlb->id;
    if (ioctl(device->fd, TENSTORRENT_IOCTL_FREE_TLB, &free_tlb) != 0) {
        return -errno;
    }

    free(tlb);

    return 0;
}

int32_t tt_noc_target_alloc(tt_noc_target_t** out_params)
{
    tt_noc_target_t* params = (tt_noc_target_t*)malloc(sizeof(tt_noc_target_t));
    if (!params) {
        return -ENOMEM;
    }
    memset(params, 0, sizeof(tt_noc_target_t));
    *out_params = params;
    return 0;
}

int32_t tt_noc_target_set_xy_end(tt_noc_target_t* params, int16_t x, int16_t y)
{
    if (!params) return -EINVAL;
    params->x_end = x;
    params->y_end = y;
    return 0;
}

int32_t tt_noc_target_set_xy_start(tt_noc_target_t* params, int16_t x, int16_t y)
{
    if (!params) return -EINVAL;
    params->x_start = x;
    params->y_start = y;
    return 0;
}

int32_t tt_noc_target_set_noc(tt_noc_target_t* params, uint8_t noc)
{
    if (!params) return -EINVAL;
    params->noc = noc;
    return 0;
}

int32_t tt_noc_target_set_mcast(tt_noc_target_t* params, uint8_t mcast)
{
    if (!params) return -EINVAL;
    params->mcast = mcast;
    return 0;
}

int32_t tt_noc_target_set_ordering(tt_noc_target_t* params, uint8_t ordering)
{
    if (!params) return -EINVAL;
    params->ordering = ordering;
    return 0;
}

int32_t tt_noc_target_set_linked(tt_noc_target_t* params, uint8_t linked)
{
    if (!params) return -EINVAL;
    params->linked = linked;
    return 0;
}

int32_t tt_noc_target_set_static_vc(tt_noc_target_t* params, uint8_t static_vc)
{
    if (!params) return -EINVAL;
    params->static_vc = static_vc;
    return 0;
}

int32_t tt_noc_target_free(tt_noc_target_t* params)
{
    if (!params) return -EINVAL;
    free(params);
    return 0;
}