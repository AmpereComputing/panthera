//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/core/l2/crp/brrip.h"

namespace ampere::core {

L2BRRIP::L2BRRIP(const sc_core::sc_module_name& name) : implements<L2BRRIP, L2RRIP>(name) {}

// FIXME: should this function take ways_mask into account?
void L2BRRIP::touch(
    uint64_t bank, uint64_t set, uint64_t way, TouchOp op, bool demand, const Set& set_contents, MemReq req)
{
    touch_stats(bank, set, way, op, demand, req);

    bool is_demand = demand || prefetch_fills_act_like_demand;
    bool is_nontemp = req && req->nontemporal;

    // Bimodal RRIP (BRRIP) sets most filled lines to the max age, which protects against thrashing.
    // A small fraction of fills are inserted with intermediate age, which allows the working set to
    // adapt over time.

    // The first hit on a prefetched line may be treated like a fill
    if (op == TouchOp::REFILL || (pf_hit_like_fill && set_contents[way].line.prefetch)) {
        // Prefetches and non-temporals fill at max age, demand at (max - delta)
        // Note this doesn't increment the counter on prefetches and non-temporals, which I think is what we want
        if (is_demand && !is_nontemp && ++brrip_counter >= brrip_long_frequency) {
            set_age(bank, set, way, op, rrip_max - insert_delta);
            brrip_counter = 0;
        } else {
            set_age(bank, set, way, op, rrip_max);
        }
    } else if (op == TouchOp::LOOKUP && !is_nontemp) {
        // Non-temporal hits don't update the age
        set_age(bank, set, way, op, 0);
    } else if (op == TouchOp::UPDATE && write_hit_updates_age && !is_nontemp) {
        set_age(bank, set, way, op, 0);
    }
}

} // namespace ampere::core
