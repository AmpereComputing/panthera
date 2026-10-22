//
// Copyright (c) 2020-2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/uarch_common/crp/rr.h"

namespace ampere::uarch_common {

void RR::init(size_t _sets, size_t _ways)
{
    ways = _ways;

    // Initialize the next-way pointers all to 0, meaning way 0 is the first
    // victim.
    set_next_way.resize(_sets, 0);
}

// RR should be updated only on fill
void RR::touch(size_t set, size_t way, TouchOp op, bool nontemporal)
{
    if (op == TouchOp::REFILL) {
        set_next_way[set] = (set_next_way[set] + 1) % ways;
    }
}

void RR::invalidate(size_t set, size_t way) {}

size_t RR::victim(size_t set_index)
{
    return set_next_way[set_index];
}

} // namespace ampere::uarch_common
