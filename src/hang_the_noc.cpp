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

        for (;;) {
            std::cout << "OK" << std::endl;
            tlb1.map(31, 28, 0);
            std::cout << "PK" << std::endl;
            auto y = tlb1.read32(0);
            std::cout << "Read: " << y << std::endl;
            auto x = device.read_telemetry(1);
            std::cout << "Telemetry: " << x << std::endl;
            std::cin >> x;
        }
    }
    return 0;
}
