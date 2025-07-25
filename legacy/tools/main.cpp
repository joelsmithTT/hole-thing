#include "device.hpp"


int main()
{
    for (auto device_path : Device::enumerate_devices()) {
        Device device(device_path);

        if (device.is_wormhole()) {
            LOG_INFO("Wormhole device found");
        } else if (device.is_blackhole()) {
            LOG_INFO("Blackhole device found");

            std::vector<uint8_t> incoming(1 << 21);
            // std::vector<uint8_t> incoming(16);
            uint64_t zero_device = 0x00000A000000ULL;
            uint64_t l2_01 = 0x000010104000;
            // auto window = device.map_tlb_2M(8, 3, l2_01, CacheMode::Uncached);
            auto window = device.map_tlb_2M(8, 3, zero_device, CacheMode::Uncached);
            window->read_block(0, incoming.data(), incoming.size());

            for (size_t i = 0; i < incoming.size(); i++) {
                if (incoming[i] != 0) {
                    LOG_INFO("Non-zero byte found at offset %zu: %02x", i, incoming[i]);
                }
            }
        }
    }
    return 0;
}