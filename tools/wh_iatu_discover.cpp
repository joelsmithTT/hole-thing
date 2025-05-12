#include "iatu.hpp"

int main()
{
    Device device("/dev/tenstorrent/0");
    wh_iatu_debug_print(device);
    return 0;
}