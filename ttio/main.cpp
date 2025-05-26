#include "ttio.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "device.hpp"
#include <vector>
#include <chrono>

int device_c()
{
    tt_device_t* device;
    uint32_t value, driver_version;
    OK(tt_device_open("/dev/tenstorrent/0", &device));
    OK(tt_driver_get_attr(device, TT_DRIVER_ATTR_VERSION, &driver_version));
    printf("driver: %08x\n", driver_version);
    OK(tt_noc_read32(device, 2, 11, 0xffb20148ULL, &value));
    printf("value: %08x\n", value);
    OK(tt_noc_read32(device, 8, 3, 0x400030000000ULL, &value));
    printf("value: %08x\n", value);

    std::vector<uint8_t> blob;
    blob.resize(1 << 28);

    auto now = std::chrono::high_resolution_clock::now();
    tt_noc_write(device, 8, 3, 0x400030000000ULL, blob.data(), blob.size());
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - now);
    printf("time: %ldms\n", duration.count());

    OK(tt_device_close(device));
    return 0;
}

int uc_vs_wc()
{
    tt_device_t* device;
    uint32_t value, driver_version;
    OK(tt_device_open("/dev/tenstorrent/0", &device));
    OK(tt_driver_get_attr(device, TT_DRIVER_ATTR_VERSION, &driver_version));
    printf("driver: %08x\n", driver_version);

    uint64_t addr = 0x400030000000ULL;
    uint16_t x = 8;
    uint16_t y = 3;

    std::vector<uint8_t> blob(1 << 21, 0xff);

    {
        tt_tlb_t* tlb_wc;
        void* mapping_wc;
        OK(tt_tlb_alloc(device, 1 << 21, &tlb_wc));
        OK(tt_tlb_mmap_wc(tlb_wc, &mapping_wc));
        OK(tt_tlb_config(tlb_wc, x, y, addr));

        auto now = std::chrono::high_resolution_clock::now();
        memcpy(mapping_wc, blob.data(), blob.size());
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - now);
        printf("wc time: %ldms\n", duration.count());
    }
    {
        tt_tlb_t* tlb_uc;
        void* mapping_uc;
        OK(tt_tlb_alloc(device, 1 << 21, &tlb_uc));
        OK(tt_tlb_mmap_uc(tlb_uc, &mapping_uc));
        OK(tt_tlb_config(tlb_uc, x, y, addr));

        auto now = std::chrono::high_resolution_clock::now();
        memcpy(mapping_uc, blob.data(), blob.size());
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - now);
        printf("uc time: %ldms\n", duration.count());
    }

    OK(tt_device_close(device));
    return 0;
}

int device_cpp()
{
    tt::Device d0("/dev/tenstorrent/0");
    tt::Device d1("/dev/tenstorrent/1");
    return 0;
}

int main()
{
    uc_vs_wc();
    return 0;
}
