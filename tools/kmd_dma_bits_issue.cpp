// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
// SPDX-License-Identifier: GPL-2.0-only

#include "device.hpp"
#include "logger.hpp"
#include <linux/mman.h>



static void test(Device& device)
{
    size_t size = 1 << 21;
    KmdDmaBuffer kmd_dmabuf(device, size);

    uint64_t iova = kmd_dmabuf.get_iova();
    if (iova & 0xffff'ffff'0000'0000) {
        LOG_INFO("KMD_DMABUF IOVA is not 32-bit");
    } else {
        LOG_INFO("KMD_DMABUF IOVA is 32-bit");
    }


    size = 1 << 30;
    HugeDmaBuffer hugepage1(device, size);
    HugeDmaBuffer hugepage2(device, size);
    HugeDmaBuffer hugepage3(device, size);


    size = 1 << 18;
    UserDmaBuffer user_dmabuf(device, size);
    iova = user_dmabuf.get_iova();
    if (iova & 0xffff'ffff'0000'0000) {
        LOG_INFO("USER_DMABUF IOVA is not 32-bit");
    } else {
        LOG_INFO("USER_DMABUF IOVA is 32-bit");
    }
}

int main()
{
    for (auto device_path : Device::enumerate_devices()) {
        Device device(device_path);
        test(device);
    }
    return 0;
}