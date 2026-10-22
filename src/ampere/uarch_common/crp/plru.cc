//
// Copyright (c) 2020-2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/uarch_common/crp/plru.h"

namespace ampere::uarch_common {

void PLRU::init(size_t _sets, size_t _ways)
{
    sets = _sets;
    ways = _ways;
    state.resize(sets, 0);
    mask.resize(ways, 0);
    val.resize(ways, 0);
    nval.resize(ways, 0);
    nt_nval.resize(ways, 0);

    AMPERE_UNCATEGORIZED_DEBUG("{} init: sets {}, ways {}", name(), sets, ways);

    switch (ways) {
    case 4:
        mask = {0b011, 0b011, 0b110, 0b110};
        val = {0b000, 0b001, 0b010, 0b110};
        nval = {0b011, 0b010, 0b100, 0b000};

        nt_nval = {0b000, 0b001, 0b010, 0b110};

        break;
    case 8:
        mask = {0b1101000, 0b1101000, 0b1100100, 0b1100100, 0b1010010, 0b1010010, 0b1010001, 0b1010001};
        val = {0b0000000, 0b0001000, 0b0100000, 0b0100100, 0b1000000, 0b1000010, 0b1010000, 0b1010001};
        nval = {0b1101000, 0b1100000, 0b1000100, 0b1000000, 0b0010010, 0b0010000, 0b0000001, 0b0000000};

        // Use same configuration for Non-Temporal accesses for 8-way plru
        nt_nval = {0b1101000, 0b1100000, 0b1000100, 0b1000000, 0b0010010, 0b0010000, 0b0000001, 0b0000000};
        break;
    default:
        AMPERE_ERROR("PLRU only supports 4-way or 8-way associativity");
        break;
    }
}

// PLRU should be updated on usage (hit or fill)
void PLRU::touch(size_t set, size_t way, TouchOp op, bool nontemporal)
{
    if (nontemporal) {
        if (op == TouchOp::REFILL) {
            state[set] &= ~mask[way];
            state[set] |= nt_nval[way];
        }
    } else {
        state[set] &= ~mask[way];
        state[set] |= nval[way];
    }
}

// Choose a victim for replacement
size_t PLRU::victim(size_t set_index)
{
    for (size_t w = 0; w < ways; w++) {
        if ((state[set_index] & mask[w]) == val[w]) {
            return w;
        }
    }
    return 0;
}

void PLRU::invalidate(size_t set, size_t way) {}

} // namespace ampere::uarch_common
