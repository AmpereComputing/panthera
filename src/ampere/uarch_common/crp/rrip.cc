//
// Copyright (c) 2020-2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/uarch_common/crp/rrip.h"

namespace ampere::uarch_common {

void RRIP::init(size_t _sets, size_t _ways)
{
    sets = _sets;
    ways = _ways;
    rrpv.resize(sets);
    for (unsigned i = 0; i < sets; i++)
        rrpv[i].resize(ways, rrip_max);
}

// RRIP should be updated on usage (hit or fill)
void RRIP::touch(size_t set, size_t way, TouchOp op, bool nontemporal)
{
    if (op == TouchOp::REFILL) {
        if (++brrip_counter >= brrip_long_frequency) {
            rrpv[set][way] = rrip_max - insert_delta;
            brrip_counter = 0;
        } else {
            rrpv[set][way] = rrip_max;
        }
    } else {
        rrpv[set][way] = 0;
    }
}

// Choose a victim for replacement
size_t RRIP::victim(size_t set_index)
{
    size_t max_way{0};
    size_t max_age{0};

    for (size_t w = 0; w < ways; ++w) {
        if (rrpv[set_index][w] > max_age) {
            max_age = rrpv[set_index][w];
            max_way = w;
        }
    }

    if (max_age < rrip_max) {
        auto increment = rrip_max - max_age;
        for (size_t w = 0; w < ways; ++w)
            rrpv[set_index][w] = std::min(rrpv[set_index][w] + increment, rrip_max.val());
    }

    return max_way;
}

void RRIP::invalidate(size_t set, size_t way)
{
    rrpv[set][way] = rrip_max;
}

} // namespace ampere::uarch_common
