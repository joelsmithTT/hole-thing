#include "iatu.hpp"


int main()
{
    Wormhole device("/dev/tenstorrent/0");
    wh_iatu_debug_print(device);
    return 0;
}