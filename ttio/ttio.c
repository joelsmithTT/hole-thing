#include "ttio.h"
#include "ioctl.h"

#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdbool.h>

#define UNUSED(x) (void)(x)
#define TWO_MEGABYTES (1UL << 21)
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define TENSTORRENT_MAX_TLBS 256

struct tt_tlb_t
{
    struct tt_device_t* device;
    int32_t id;
    size_t size;
    void* mmio;
};

struct tt_noc_params_t
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

struct tt_device_t
{
    int fd;

    int32_t dmabufs[TENSTORRENT_MAX_DMA_BUFS];
    struct tt_tlb_t tlbs[TENSTORRENT_MAX_TLBS];
};

int32_t tt_device_open(const char* chardev_path, tt_device_t** out_device)
{
    struct tt_device_t* dev = (struct tt_device_t*)malloc(sizeof(struct tt_device_t));

    if (!dev) return -ENOMEM;

    memset(dev, 0, sizeof(struct tt_device_t));

    dev->fd = open(chardev_path, O_RDWR | O_CLOEXEC);
    if (dev->fd == -1) {
        free(dev);
        return -errno;
    }

    *out_device = dev;

    return 0;
}

int32_t tt_device_close(tt_device_t* device)
{
    int ret = 0;

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
    UNUSED(device);
    UNUSED(device_id);
}

int32_t tt_device_get_attr(tt_device_t* device, uint32_t attr, void* out_value)
{
    struct tenstorrent_get_device_info get_device_info = {0};

    get_device_info.in.output_size_bytes = sizeof(get_device_info.out);

    if (ioctl(device->fd, TENSTORRENT_IOCTL_GET_DEVICE_INFO, &get_device_info) != 0)
        return -errno;

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

    get_driver_info.in.output_size_bytes = sizeof(get_driver_info.out);

    if (ioctl(device->fd, TENSTORRENT_IOCTL_GET_DRIVER_INFO, &get_driver_info) != 0) return -errno;

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

    if (ioctl(device->fd, TENSTORRENT_IOCTL_PIN_PAGES, &pin) != 0) return -errno;

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

    buf_index = find_free_dmabuf(device);

    if (buf_index == -1) return -ENOMEM;
    device->dmabufs[buf_index] = 1;

    dmabuf.in.requested_size = size;
    dmabuf.in.flags = TENSTORRENT_ALLOCATE_DMA_BUF_NOC_DMA;
    dmabuf.in.buf_index = (uint8_t)buf_index;

    if (ioctl(device->fd, TENSTORRENT_IOCTL_ALLOCATE_DMA_BUF, &dmabuf) != 0) {
        device->dmabufs[buf_index] = 0;
        return -errno;
    }

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

int32_t tt_tlb_alloc(tt_device_t* device, size_t size, enum tt_mmio_cache_mode mode, tt_tlb_t** out_tlb)
{
    struct tenstorrent_allocate_tlb alloc_tlb = {0};
    alloc_tlb.in.size = size;

    if (ioctl(device->fd, TENSTORRENT_IOCTL_ALLOCATE_TLB, &alloc_tlb) != 0) {
        return -errno;
    }

    struct tt_tlb_t* tlb = &device->tlbs[alloc_tlb.out.id];
    tlb->id = alloc_tlb.out.id;
    tlb->device = device;
    tlb->size = size;

    off_t offset = mode == TT_MMIO_CACHE_MODE_UC ? alloc_tlb.out.mmap_offset_uc : alloc_tlb.out.mmap_offset_wc;
    void* mmio = mmap(NULL, tlb->size, PROT_READ | PROT_WRITE, MAP_SHARED, device->fd, offset);
    if (mmio == MAP_FAILED) {
        memset(tlb, 0, sizeof(struct tt_tlb_t));
        return -errno;
    }

    tlb->mmio = mmio;

    *out_tlb = tlb;

    return 0;
}

int32_t tt_tlb_free(tt_tlb_t* tlb)
{
    struct tenstorrent_free_tlb free_tlb = {0};
    free_tlb.in.id = tlb->id;

    munmap(tlb->mmio, tlb->size);

    if (ioctl(tlb->device->fd, TENSTORRENT_IOCTL_FREE_TLB, &free_tlb) != 0) {
        return -errno;
    }

    memset(tlb, 0, sizeof(struct tt_tlb_t));

    return 0;
}

int32_t tt_tlb_get_mmio(tt_tlb_t* tlb, void** out_mmio)
{
    *out_mmio = tlb->mmio;
    return 0;
}

int32_t tt_tlb_set_params(tt_tlb_t* tlb, tt_noc_params_t* params)
{
    struct tenstorrent_configure_tlb configure_tlb = {0};
    configure_tlb.in.id = tlb->id;
    configure_tlb.in.config.addr = params->addr;
    configure_tlb.in.config.x_end = params->x_end;
    configure_tlb.in.config.y_end = params->y_end;
    configure_tlb.in.config.x_start = params->x_start;
    configure_tlb.in.config.y_start = params->y_start;
    configure_tlb.in.config.noc = params->noc;
    configure_tlb.in.config.mcast = params->mcast;
    configure_tlb.in.config.ordering = params->ordering;
    configure_tlb.in.config.linked = params->linked;
    configure_tlb.in.config.static_vc = params->static_vc;

    if (ioctl(tlb->device->fd, TENSTORRENT_IOCTL_CONFIGURE_TLB, &configure_tlb) != 0) {
        return -errno;
    }

    return 0;
}

int32_t tt_tlb_set(tt_tlb_t* tlb, uint16_t x, uint16_t y, uint64_t addr)
{
    struct tenstorrent_configure_tlb configure_tlb = {0};
    configure_tlb.in.id = tlb->id;
    configure_tlb.in.config.addr = addr;
    configure_tlb.in.config.x_end = x;
    configure_tlb.in.config.y_end = y;

    if (ioctl(tlb->device->fd, TENSTORRENT_IOCTL_CONFIGURE_TLB, &configure_tlb) != 0) {
        return -errno;
    }

    return 0;
}


// TODO: This is stupid, instead we should:
// 1. Get the driver to do it (e.g. TENSTORRENT_IOCTL_NOC_ACCESS, not implemented).
// 2. Allocate and map some TLB windows during tt_device_open:
//    - keep one or more in the tt_device_t
//    - grab one for exclusive access during tt_noc_read32, tt_noc_write32, tt_noc_read, tt_noc_write
//    - put it back when done
int32_t tt_noc_read32(tt_device_t* device, uint16_t x, uint16_t y, uint64_t addr, uint32_t* value)
{
    if (!value) return -EINVAL;
    if (addr % 4 != 0) return -EINVAL;

    tt_tlb_t* tlb;
    int32_t ret = tt_tlb_alloc(device, TWO_MEGABYTES, TT_MMIO_CACHE_MODE_UC, &tlb);
    if (ret != 0) {
        return ret;
    }

    uint64_t aligned_addr = addr & ~(tlb->size - 1);
    uint64_t offset = addr & (tlb->size - 1);
    struct tt_noc_params_t params = {0};
    params.addr = aligned_addr;
    params.x_end = x;
    params.y_end = y;
    params.noc = 0;
    params.ordering = TT_NOC_ORDERING_STRICT;

    ret = tt_tlb_set_params(tlb, &params);

    if (ret != 0) {
        tt_tlb_free(tlb);
        return ret;
    }

    *value = *(volatile uint32_t*)((uint8_t*)tlb->mmio + offset);

    tt_tlb_free(tlb);
    return 0;
}

int32_t tt_noc_write32(tt_device_t* device, uint16_t x, uint16_t y, uint64_t addr, uint32_t value)
{
    if (addr % 4 != 0) return -EINVAL;

    tt_tlb_t* tlb;
    int32_t ret = tt_tlb_alloc(device, TWO_MEGABYTES, TT_MMIO_CACHE_MODE_UC, &tlb);
    if (ret != 0) {
        return ret;
    }

    uint64_t aligned_addr = addr & ~(tlb->size - 1);
    uint64_t offset = addr & (tlb->size - 1);
    struct tt_noc_params_t params = {0};
    params.addr = aligned_addr;
    params.x_end = x;
    params.y_end = y;
    params.noc = 0;
    params.ordering = TT_NOC_ORDERING_STRICT;

    ret = tt_tlb_set_params(tlb, &params);

    if (ret != 0) {
        tt_tlb_free(tlb);
        return ret;
    }

    *(volatile uint32_t*)((uint8_t*)tlb->mmio + offset) = value;

    tt_tlb_free(tlb);
    return 0;
}

int32_t tt_noc_read(tt_device_t* device, uint16_t x, uint16_t y, uint64_t addr, void* dst, size_t size)
{
    struct tt_noc_params_t params = {0};
    tt_tlb_t* tlb;
    uint8_t* dst_ptr;
    int32_t ret;

    if (addr % 4 != 0) return -EINVAL;
    if (size % 4 != 0) return -EINVAL;

    ret = tt_tlb_alloc(device, TWO_MEGABYTES, TT_MMIO_CACHE_MODE_WC, &tlb);
    if (ret != 0) {
        return ret;
    }

    params.x_end = x;
    params.y_end = y;
    params.noc = 0;
    dst_ptr = (uint8_t*)dst;

    while (size > 0) {
        uint64_t aligned_addr = addr & ~(tlb->size - 1);
        uint64_t offset = addr & (tlb->size - 1);
        size_t chunk_size = MIN(size, tlb->size - offset);
        uint8_t* src_ptr = (uint8_t*)tlb->mmio + offset;

        params.addr = aligned_addr;
        ret = tt_tlb_set_params(tlb, &params);

        if (ret != 0) {
            tt_tlb_free(tlb);
            return ret;
        }

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
    struct tt_noc_params_t params = {0};
    tt_tlb_t* tlb;
    uint8_t* src_ptr;
    int32_t ret;

    if (addr % 4 != 0) return -EINVAL;
    if (size % 4 != 0) return -EINVAL;

    ret = tt_tlb_alloc(device, TWO_MEGABYTES, TT_MMIO_CACHE_MODE_WC, &tlb);
    if (ret != 0) {
        return ret;
    }

    params.x_end = x;
    params.y_end = y;
    params.noc = 0;
    src_ptr = (uint8_t*)src;

    while (size > 0) {
        uint64_t aligned_addr = addr & ~(tlb->size - 1);
        uint64_t offset = addr & (tlb->size - 1);
        size_t chunk_size = MIN(size, tlb->size - offset);
        uint8_t* dst_ptr = (uint8_t*)tlb->mmio + offset;

        ret = tt_tlb_write_config(tlb, x, y, aligned_addr);

        if (ret != 0) {
            tt_tlb_free(tlb);
            return ret;
        }

        memcpy(dst_ptr, src_ptr, chunk_size);

        src_ptr += chunk_size;
        size -= chunk_size;
        addr += chunk_size;
    }

    tt_tlb_free(tlb);
    return 0;
}

int32_t tt_noc_params_alloc(tt_noc_params_t** out_params)
{
    tt_noc_params_t* params = (tt_noc_params_t*)malloc(sizeof(tt_noc_params_t));
    if (!params) {
        return -ENOMEM;
    }
    memset(params, 0, sizeof(tt_noc_params_t));
    *out_params = params;
    return 0;
}

int32_t tt_noc_params_set_xy_end(tt_noc_params_t* params, int16_t x, int16_t y)
{
    params->x_end = x;
    params->y_end = y;
    return 0;
}

int32_t tt_noc_params_set_xy_start(tt_noc_params_t* params, int16_t x, int16_t y)
{
    params->x_start = x;
    params->y_start = y;
    return 0;
}

int32_t tt_noc_params_set_noc(tt_noc_params_t* params, uint8_t noc)
{
    params->noc = noc;
    return 0;
}

int32_t tt_noc_params_set_mcast(tt_noc_params_t* params, bool mcast)
{
    params->mcast = mcast;
    return 0;
}

int32_t tt_noc_params_set_ordering(tt_noc_params_t* params, enum tt_noc_ordering ordering)
{
    params->ordering = ordering;
    return 0;
}

int32_t tt_noc_params_set_linked(tt_noc_params_t* params, bool linked)
{
    params->linked = linked;
    return 0;
}

int32_t tt_noc_params_set_static_vc(tt_noc_params_t* params, uint8_t static_vc)
{
    params->static_vc = static_vc;
    return 0;
}

int32_t tt_noc_params_free(tt_noc_params_t* params)
{
    free(params);
    return 0;
}
