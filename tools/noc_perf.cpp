#include "device.hpp"
#include "types.hpp"
#include <iostream>
#include <map>
#include <thread>
#define NIU_SLV_POSTED_WR_DATA_WORD_RECEIVED      0x39
#define NIU_SLV_NONPOSTED_WR_DATA_WORD_RECEIVED   0x38
#define NIU_SLV_RD_DATA_WORD_SENT             0x33
#define NIU_MST_POSTED_WR_DATA_WORD_SENT      0x9
#define NIU_MST_NONPOSTED_WR_DATA_WORD_SENT   0x8
#define NIU_MST_RD_DATA_WORD_RECEIVED         0x3

std::map<uint64_t, std::string> NIU_REG_NAMES = {
    {NIU_SLV_POSTED_WR_DATA_WORD_RECEIVED, "NIU_SLV_POSTED_WR_DATA_WORD_RECEIVED"},
    {NIU_SLV_NONPOSTED_WR_DATA_WORD_RECEIVED, "NIU_SLV_NONPOSTED_WR_DATA_WORD_RECEIVED"},
    {NIU_SLV_RD_DATA_WORD_SENT, "NIU_SLV_RD_DATA_WORD_SENT"},
    {NIU_MST_POSTED_WR_DATA_WORD_SENT, "NIU_MST_POSTED_WR_DATA_WORD_SENT"},
    {NIU_MST_NONPOSTED_WR_DATA_WORD_SENT, "NIU_MST_NONPOSTED_WR_DATA_WORD_SENT"},
    {NIU_MST_RD_DATA_WORD_RECEIVED, "NIU_MST_RD_DATA_WORD_RECEIVED"},
};

int bar4_edition()
{
    Device device("/dev/tenstorrent/0");
    auto bar4 = device.get_bar4();
    return 0;
}

int main()
{
    Device device("/dev/tenstorrent/0");
    auto [pcie_x, pcie_y] = device.get_pcie_coordinates();

    static constexpr uint64_t PCIE_NOC_REG_BASE = 0xFFFB20000ULL;
    auto window0 = device.map_tlb_2M(pcie_x, pcie_y, PCIE_NOC_REG_BASE, CacheMode::WriteCombined);
    // auto window0 = device.map_tlb_2M(pcie_x, pcie_y, PCIE_NOC_REG_BASE, CacheMode::WriteCombined);

    for (;;) {
        for (const auto& [reg, name] : NIU_REG_NAMES) {
            auto value = window0->read32(0x200 + 4 * reg);
            std::cout << name << ": " << value << std::endl;
        }
        std::cout << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    return 0;
}