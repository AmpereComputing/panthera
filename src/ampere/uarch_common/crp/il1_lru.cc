//
// Copyright (c) 2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/uarch_common/crp/il1_lru.h"

namespace ampere::uarch_common {

void IL1LRU::init(size_t _sets, size_t _ways)
{
    sets = _sets;
    ways = _ways;

    // Initialize the sets/ways structure to 0.  All same oldest age
    set_way_age.resize(sets);
    for (unsigned i = 0; i < sets; i++)
        set_way_age[i].resize(ways, 0);
}

// LRU should be updated on usage (hit or fill)
void IL1LRU::touch(size_t set, size_t way, TouchOp op, bool nontemporal)
{
    set_way_age[set][way] = ++age;
}

void IL1LRU::invalidate(size_t set, size_t way)
{
    // Do not update the age on invalidation -- victim is chosen based on LRU even if there are invalid lines
}

// Choose a victim for replacement
size_t IL1LRU::victim(size_t set_index)
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
