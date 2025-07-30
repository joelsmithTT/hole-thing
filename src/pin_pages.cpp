#include "holething.hpp"
#include <tenstorrent/ttkmd.h>

#include <iostream>
#include <string>

using namespace tt;

int main()
{
    for (auto device_path : DeviceUtils::enumerate_devices()) {
        const size_t size = 0x1000 + (18ULL * (1ULL << 30));
        Device device(device_path.c_str());

        if (device.is_wormhole()) {
            continue;
        }

        DmaBuffer buf(device, size, TT_DMA_FLAG_NOC);
        TlbWindow tlb(device, 1<<21, TT_MMIO_CACHE_MODE_WC);
        auto [x, y] = device.get_pcie_coordinates();


        std::vector<uint64_t> random_data(size / sizeof(uint64_t));
        for (size_t i = 0; i < random_data.size(); ++i)
        {
            random_data[i] = i;
        }
        TlbWindowUtils::noc_write(tlb, x, y, buf.get_noc_addr(), &random_data[0], size);

        std::cout << "Ok" << std::endl;

        void *a = buf.get_mem();
        void *b = random_data.data();

        uint8_t* a_ptr = (uint8_t*)a;
        uint8_t* b_ptr = (uint8_t*)b;
        for (size_t i = 0; i < size; ++i) {
            if (a_ptr[i] != b_ptr[i]) {
                std::cerr << "Data mismatch at index " << i << ": "
                          << std::hex << static_cast<int>(a_ptr[i]) << " != "
                          << static_cast<int>(b_ptr[i]) << std::dec << std::endl;
                return 1;
            }
        }


        DmaBuffer buf2(device, size, TT_DMA_FLAG_NOC);
        TlbWindow tlb2(device, 1ULL<<32, TT_MMIO_CACHE_MODE_WC);

        std::vector<uint64_t> random_data2(size / sizeof(uint64_t));
        for (size_t i = 0; i < random_data2.size(); ++i)
        {
            random_data[i] = i;
        }
        TlbWindowUtils::noc_write(tlb2, x, y, buf2.get_noc_addr(), &random_data2[0], size);

        std::cout << "Ok" << std::endl;

        void *a2 = buf2.get_mem();
        void *b2 = random_data2.data();

        a_ptr = (uint8_t*)a2;
        b_ptr = (uint8_t*)b2;
        for (size_t i = 0; i < size; ++i) {
            if (a_ptr[i] != b_ptr[i]) {
                std::cerr << "Data mismatch at index " << i << ": "
                          << std::hex << static_cast<int>(a_ptr[i]) << " != "
                          << static_cast<int>(b_ptr[i]) << std::dec << std::endl;
                return 1;
            }
        }
    }
    return 0;
}
