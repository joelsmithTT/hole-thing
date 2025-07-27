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

        int x;
        std::cin >> x;
    }
    return 0;
}
