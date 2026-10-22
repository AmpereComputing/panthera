//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/core/l2/crp/random.h"

namespace ampere::core {

void L2Random::init(uint64_t _banks, uint64_t _sets, uint64_t _ways, uint64_t _ways_mask)
{
    banks = _banks;
    sets = _sets;
    ways = _ways;
    ways_mask = _ways_mask;
}

void L2Random::touch(
    uint64_t bank, uint64_t set, uint64_t way, TouchOp op, bool demand, const Set& set_contents, MemReq req)
{
}

// Choose a victim for replacement
uint64_t L2Random::victim(uint64_t bank, uint64_t set, const Set& set_contents, uint64_t way_mask)
{
    return random.get<uint64_t>(0, ways - 1);
}

void L2Random::invalidate(uint64_t bank, uint64_t set, uint64_t way) {}

} // namespace ampere::core
