// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
// SPDX-License-Identifier: GPL-2.0-only

#pragma once

#include <cstdint>

enum CacheMode { Uncached, WriteCombined };
enum CardType { WORMHOLE, BLACKHOLE };

struct coord_t
{
    uint32_t x;
    uint32_t y;
};
