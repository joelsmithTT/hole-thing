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

void to_device(Device& device, int noc, size_t bytes)
{
    auto window = device.map_tlb_2M(0, 0, 0, CacheMode::Uncached, noc);

    for (size_t i = 0; i < bytes; i += 4) {
        window->write32(0, 0);
    }
}

std::map<std::string, uint64_t> dump_stats(Device& device)
{
    auto [pcie_x, pcie_y] = device.get_pcie_coordinates();
    auto [size_x, size_y] = device.get_noc_grid_size();
    auto window0 = device.map_tlb_2M(pcie_x, pcie_y, PCIE_NOC_REG_BASE, CacheMode::Uncached, 0);
    auto window1 = device.map_tlb_2M(pcie_x, pcie_y, PCIE_NOC_REG_BASE, CacheMode::Uncached, 1);

    std::map<std::string, uint64_t> stats;

    for (const auto& [reg, name] : NIU_REG_NAMES) {
        auto value0 = window0->read32(0x200 + 4 * reg);
        auto value1 = window1->read32(0x200 + 4 * reg);
        std::cout << name << "0: " << value0 << std::endl;
        std::cout << name << "1: " << value1 << std::endl;

        stats[name + "0"] = value0;
        stats[name + "1"] = value1;
    }

    return stats;
}

int main()
{
    Device device("/dev/tenstorrent/0");

    for (;;){
        // to_device(device, 1, 1024);
        dump_stats(device);
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
    dump_stats(device);
    to_device(device, 0, 1024);
    dump_stats(device);

    return 0;

    auto [pcie_x, pcie_y] = device.get_pcie_coordinates();
    auto [size_x, size_y] = device.get_noc_grid_size();
    auto window0 = device.map_tlb_2M(pcie_x, pcie_y, PCIE_NOC_REG_BASE, CacheMode::Uncached, 0);
    auto window1 = device.map_tlb_2M(size_x - 1 - pcie_x, size_y - 1 - pcie_y, PCIE_NOC_REG_BASE, CacheMode::Uncached, 1);

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