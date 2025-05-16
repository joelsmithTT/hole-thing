// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
// SPDX-License-Identifier: GPL-2.0-only

#include "device.hpp"
#include "types.hpp"
#include <iostream>
#include <map>
#include <thread>

static constexpr uint64_t NIU_SLV_POSTED_WR_DATA_WORD_RECEIVED = 0x39;
static constexpr uint64_t NIU_SLV_NONPOSTED_WR_DATA_WORD_RECEIVED = 0x38;
static constexpr uint64_t NIU_SLV_RD_DATA_WORD_SENT = 0x33;
static constexpr uint64_t NIU_MST_POSTED_WR_DATA_WORD_SENT = 0x9;
static constexpr uint64_t NIU_MST_NONPOSTED_WR_DATA_WORD_SENT = 0x8;
static constexpr uint64_t NIU_MST_RD_DATA_WORD_RECEIVED = 0x3;

static constexpr uint64_t PCIE_NOC_REG_BASE = 0xFFFB20000ULL;

std::map<uint64_t, std::string> NIU_REG_NAMES = {
    {NIU_SLV_POSTED_WR_DATA_WORD_RECEIVED, "NIU_SLV_POSTED_WR_DATA_WORD_RECEIVED"},
    {NIU_SLV_NONPOSTED_WR_DATA_WORD_RECEIVED, "NIU_SLV_NONPOSTED_WR_DATA_WORD_RECEIVED"},
    {NIU_SLV_RD_DATA_WORD_SENT, "NIU_SLV_RD_DATA_WORD_SENT"},
    {NIU_MST_POSTED_WR_DATA_WORD_SENT, "NIU_MST_POSTED_WR_DATA_WORD_SENT"},
    {NIU_MST_NONPOSTED_WR_DATA_WORD_SENT, "NIU_MST_NONPOSTED_WR_DATA_WORD_SENT"},
    {NIU_MST_RD_DATA_WORD_RECEIVED, "NIU_MST_RD_DATA_WORD_RECEIVED"},
};

int main()
{
    Device device("/dev/tenstorrent/0");

    auto [pcie_x, pcie_y] = device.get_pcie_coordinates();
    auto window0 = device.map_tlb_2M(pcie_x, pcie_y, PCIE_NOC_REG_BASE, CacheMode::Uncached);
    auto window1 = device.map_tlb_2M(pcie_x, pcie_y, PCIE_NOC_REG_BASE, CacheMode::Uncached);

    for (;;) {
        for (const auto& [reg, name] : NIU_REG_NAMES) {
            auto value0 = window0->read32(0x200 + 4 * reg);
            auto value1 = window1->read32(0x200 + 4 * reg);
            std::cout << name << "0: " << value0 << std::endl;
            std::cout << name << "1: " << value1 << std::endl;
        }
        std::cout << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    return 0;
}