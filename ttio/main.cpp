#include "ttio.h"

#include <stdio.h>
#include <stdlib.h>

#include "device.hpp"


int device_c()
{
    tt_device_t* device;
    uint32_t value;
    OK(tt_device_open("/dev/tenstorrent/0", &device));
    OK(tt_noc_read32(device, 2, 11, 0xffb20148ULL, &value));
    printf("value: %08x\n", value);
    OK(tt_device_close(device));
    OK(tt_device_open("/dev/tenstorrent/1", &device));
    OK(tt_device_close(device));
    return 0;
}

int device_cpp()
{
    tt::Device d0("/dev/tenstorrent/0");
    tt::Device d1("/dev/tenstorrent/1");
    return 0;
}

int main()
{
    device_cpp();
    return 0;
}