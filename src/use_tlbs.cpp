#include "holething.hpp"
#include <tenstorrent/ttkmd.h>

#include <iostream>
#include <string>

using namespace tt;

int main()
{
    for (auto device_path : DeviceUtils::enumerate_devices()) {
        Device device(device_path.c_str());
        TlbWindow tlb1(device, 1ULL << 21, TT_MMIO_CACHE_MODE_UC);
        TlbWindow tlb2(device, 1ULL << 21, TT_MMIO_CACHE_MODE_UC);
        TlbWindow tlb3(device, 1ULL << 21, TT_MMIO_CACHE_MODE_UC);
        TlbWindow tlb4(device, 1ULL << 21, TT_MMIO_CACHE_MODE_UC);

        tlb1.map(0, 0, 0x0);

        int x;
        std::cin >> x;

        tlb1.read32(0);
    }
    return 0;
}
