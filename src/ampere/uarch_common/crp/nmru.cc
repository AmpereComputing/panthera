//
// Copyright (c) 2020-2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/uarch_common/crp/nmru.h"

namespace ampere::uarch_common {

void NMRU::init(size_t _sets, size_t _ways)
{
    sets = _sets;
    ways = _ways;
    start_ptr.resize(sets, 0);
    r_value.resize(sets);
    for (unsigned i = 0; i < sets; i++) {
        r_value[i].resize(ways, 0);
    }
}

// NMRU should be updated on usage (hit or fill)
void NMRU::touch(size_t set, size_t way, TouchOp op, bool nontemporal)
{
    // first touch r_value
    r_value[set][way] = 1;
    auto checker = r_value[set];
    std::vector<unsigned>::iterator it;
    it = find(checker.begin(), checker.end(), 0);
    if (it == checker.end()) {
        for (unsigned i = 0; i < ways; i++) {
            r_value[set][i] = 0;
        }
        r_value[set][way] = 1;
    }

    // On writes to the cacheline we need to increment the starting spot for
    // selecting a victim in a round robin fasion
    if (op != TouchOp::LOOKUP)
        start_ptr[set] = (start_ptr[set] + 1) % ways;
}

// Choose a victim for replacement
size_t NMRU::victim(size_t set_index)
{
    // choose victim while rotating the start searching victim
    for (unsigned j = 0; j < ways; j++) {
        unsigned i = (start_ptr[set_index] + j) % ways;
        if (r_value[set_index][i] == 0) {
            return i;
        }
    }

    return 0;
}

void NMRU::invalidate(size_t set, size_t way) {}

} // namespace ampere::uarch_common
