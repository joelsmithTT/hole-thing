#include "holething.hpp"
#include <tenstorrent/ttkmd.h>

#include <iostream>
#include <string>

int main()
{
    for (auto device_path : tt::DeviceUtils::enumerate_devices()) {
        tt::Device device(device_path.c_str());
        tt::DmaBuffer dma1(device, 0x1000, TT_DMA_FLAG_NOC);
        tt::DmaBuffer dma2(device, 0x2000, TT_DMA_FLAG_NOC_TOP_DOWN);
        tt::DmaBuffer dma3(device, 0x4000, TT_DMA_FLAG_NONE);
        std::cout << "Wait..." << std::endl;
        int x;
        std::cin >> x;
    }
    return 0;
}
