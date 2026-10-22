//
// Copyright (c) 2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/uarch_common/crp/fifo.h"

namespace ampere::uarch_common {

void FIFO::init(size_t _sets, size_t _ways)
{
    sets = _sets;
    ways = _ways;

    // Initialize the sets/ways structure to 0.  All same oldest age
    set_way_age.resize(sets);
    for (unsigned i = 0; i < sets; i++)
        set_way_age[i].resize(ways, 0);
}

// FIFO should be updated only on fill
void FIFO::touch(size_t set, size_t way, TouchOp op, bool nontemporal)
{
    if (op == TouchOp::REFILL)
        set_way_age[set][way] = ++age;
}

void FIFO::invalidate(size_t set, size_t way)
{
    if (set < set_way_age.size())
        if (way < set_way_age[set].size())
            set_way_age[set][way] = 0;
}

// Choose a victim for replacement
size_t FIFO::victim(size_t set_index)
{
    size_t chosen{ways};
    uint64_t chosen_age{UINT64_MAX};

    for (unsigned i = 0; i < ways; i++) {
        auto way_age = set_way_age[set_index][i];
        if (way_age < chosen_age) {
            chosen_age = way_age;
            chosen = i;
        }
    }

    return chosen;
}

} // namespace ampere::uarch_common
