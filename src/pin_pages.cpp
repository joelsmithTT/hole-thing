#include "holething.hpp"
#include <tenstorrent/ttkmd.h>

#include <iostream>
#include <string>

using namespace tt;

int main()
{
    for (auto device_path : DeviceUtils::enumerate_devices()) {
        Device device(device_path.c_str());
        DmaBuffer dma1(device, 0x1000, TT_DMA_FLAG_NOC);
        DmaBuffer dma2(device, 0x2000, TT_DMA_FLAG_NOC_TOP_DOWN);
        DmaBuffer dma3(device, 0x4000, TT_DMA_FLAG_NONE);

        int x;
        std::cin >> x;
    }
    return 0;
}
