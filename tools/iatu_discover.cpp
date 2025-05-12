#include "iatu.hpp"

int main()
{
    for (auto device_path : enumerate_devices()) {
        Device device(device_path);

        if (device.is_wormhole()) {
            wh_iatu_debug_print(device);
        } else if (device.is_blackhole()) {
            bh_iatu_debug_print(device);
        } else {
            auto info = device.get_device_info();
            auto id = info.device_id;
            LOG_ERROR("What is this device id? %04x", id);
        }
    }
    return 0;
}