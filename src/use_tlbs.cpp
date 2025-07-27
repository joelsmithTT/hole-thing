#include "holething.hpp"
#include <tenstorrent/ttkmd.h>

#include <iostream>
#include <string>

int main()
{
    for (auto device_path : tt::DeviceUtils::enumerate_devices()) {
        tt::Device device(device_path.c_str());
        tt::TlbWindow tlb1(device, 1ULL << 21, TT_MMIO_CACHE_MODE_UC);
        tt::TlbWindow tlb2(device, 1ULL << 21, TT_MMIO_CACHE_MODE_UC);
        tt::TlbWindow tlb3(device, 1ULL << 21, TT_MMIO_CACHE_MODE_UC);
        tt::TlbWindow tlb4(device, 1ULL << 21, TT_MMIO_CACHE_MODE_UC);

    }
    return 0;
}
