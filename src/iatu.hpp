#pragma once

#include "device.hpp"
#include "utility.hpp"

static const uint64_t WH_IATU_REGS = 0x8'0030'0000;    // In PCIe, with DBI bits set.
static const uint64_t BH_IATU_REGS = 0xf800000000300000ULL;
static const size_t NUM_IATU_REGIONS = 16;

static const uint64_t WH_IATU_BAR2_OFFSET = 0x1200;
static const uint64_t BH_IATU_BAR2_OFFSET = 0x1000;

struct IatuRegion
{
    uint32_t outbound_ctrl1;       // Offset 0x00 (Relative to region start)
    uint32_t outbound_ctrl2;       // Offset 0x04
    uint32_t outbound_base_lo;     // Offset 0x08
    uint32_t outbound_base_hi;     // Offset 0x0C
    uint32_t outbound_limit;       // Offset 0x10
    uint32_t outbound_target_lo;   // Offset 0x14
    uint32_t outbound_target_hi;   // Offset 0x18
    uint32_t reserved_1c;          // Offset 0x1C
    uint32_t outbound_limit_hi;    // Offset 0x20
    uint32_t reserved_outbound_padding[55]; // Offsets 0x24 to 0xFC

    uint32_t inbound_ctrl1;        // Offset 0x100
    uint32_t inbound_ctrl2;        // Offset 0x104
    uint32_t inbound_base_lo;      // Offset 0x108
    uint32_t inbound_base_hi;      // Offset 0x10C
    uint32_t inbound_limit;        // Offset 0x110
    uint32_t inbound_target_lo;    // Offset 0x114
    uint32_t inbound_target_hi;    // Offset 0x118
    uint32_t reserved_11c;         // Offset 0x11C
    uint32_t inbound_limit_hi;     // Offset 0x120
    uint32_t reserved_inbound_padding[55]; // Offsets 0x124 to 0x1FC
};

struct IatuRegisters
{
    struct IatuRegion regions[NUM_IATU_REGIONS];
};


inline void wh_iatu_debug_print(Device& device)
{
    auto& bar2 = device.get_bar2();
    auto [x, y] = device.get_pcie_coordinates();
    auto dbi_tlb_window = device.map_tlb_2M(x, y, WH_IATU_REGS, CacheMode::Uncached);
    auto* iatus = dbi_tlb_window->as_ptr<volatile IatuRegisters>();

    LOG_INFO("Wormhole iATU Registers as seen from DBI:");
    device.enable_dbi(true);
    for (size_t region = 0; region < NUM_IATU_REGIONS; region++) {
        auto& iatu = iatus->regions[region];
        auto base = (static_cast<uint64_t>(iatu.outbound_base_hi) << 32) | iatu.outbound_base_lo;
        auto limit = (static_cast<uint64_t>(iatu.outbound_limit_hi) << 32) | iatu.outbound_limit;
        auto target = (static_cast<uint64_t>(iatu.outbound_target_hi) << 32) | iatu.outbound_target_lo;

        LOG_INFO("IATU Region %zu: Base: 0x%016lx, Limit: 0x%016lx, Target: 0x%016lx", region, base, limit, target);
    }
    device.enable_dbi(false);

    LOG_INFO("Wormhole iATU Registers as seen from BAR2:");
    for (size_t region = 0; region < NUM_IATU_REGIONS; region++) {
        auto& iatu = bar2.as_ptr<volatile IatuRegisters>(0x1200)->regions[region];
        auto base = (static_cast<uint64_t>(iatu.outbound_base_hi) << 32) | iatu.outbound_base_lo;
        auto limit = (static_cast<uint64_t>(iatu.outbound_limit_hi) << 32) | iatu.outbound_limit;
        auto target = (static_cast<uint64_t>(iatu.outbound_target_hi) << 32) | iatu.outbound_target_lo;

        LOG_INFO("IATU Region %zu: Base: 0x%016lx, Limit: 0x%016lx, Target: 0x%016lx", region, base, limit, target);
    }
}

inline void bh_iatu_debug_print(Device& device)
{
    auto& bar2 = device.get_bar2();
    auto [x, y] = device.get_pcie_coordinates();
    auto dbi_tlb_window = device.map_tlb_2M(x, y, BH_IATU_REGS, CacheMode::Uncached);
    auto* iatus = dbi_tlb_window->as_ptr<volatile IatuRegisters>();

    LOG_INFO("Blackhole iATU Registers as seen from DBI:");
    for (size_t region = 0; region < NUM_IATU_REGIONS; region++) {
        auto& iatu = iatus->regions[region];
        auto base = (static_cast<uint64_t>(iatu.outbound_base_hi) << 32) | iatu.outbound_base_lo;
        auto limit = (static_cast<uint64_t>(iatu.outbound_limit_hi) << 32) | iatu.outbound_limit;
        auto target = (static_cast<uint64_t>(iatu.outbound_target_hi) << 32) | iatu.outbound_target_lo;

        LOG_INFO("IATU Region %zu: Base: 0x%016lx, Limit: 0x%016lx, Target: 0x%016lx", region, base, limit, target);
    }

    LOG_INFO("Blackhole iATU Registers as seen from BAR2:");
    for (size_t region = 0; region < NUM_IATU_REGIONS; region++) {
        auto& iatu = bar2.as_ptr<volatile IatuRegisters>(BH_IATU_BAR2_OFFSET)->regions[region];
        auto base = (static_cast<uint64_t>(iatu.outbound_base_hi) << 32) | iatu.outbound_base_lo;
        auto limit = (static_cast<uint64_t>(iatu.outbound_limit_hi) << 32) | iatu.outbound_limit;
        auto target = (static_cast<uint64_t>(iatu.outbound_target_hi) << 32) | iatu.outbound_target_lo;

        LOG_INFO("IATU Region %zu: Base: 0x%016lx, Limit: 0x%016lx, Target: 0x%016lx", region, base, limit, target);
    }
}