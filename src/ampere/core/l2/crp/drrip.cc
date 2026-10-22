//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/core/l2/crp/drrip.h"

namespace ampere::core {

L2DRRIP::L2DRRIP(const sc_core::sc_module_name& name) : implements<L2DRRIP, L2RRIP>(name) {}

bool L2DRRIP::dedicated_srrip(uint64_t set)
{
    return (set & set_dueling_mask) == dedicated_srrip_match;
}

bool L2DRRIP::dedicated_brrip(uint64_t set)
{
    return (set & set_dueling_mask) == dedicated_brrip_match;
}

// FIXME: should this function take ways_mask into account?
void L2DRRIP::touch(
    uint64_t bank, uint64_t set, uint64_t way, TouchOp op, bool demand, const Set& set_contents, MemReq req)
{
    touch_stats(bank, set, way, op, demand, req);

    bool is_demand = demand || prefetch_fills_act_like_demand;
    bool is_nontemp = req && req->nontemporal;

    // Policy selection counter is adjusted on true fills of any kind (not prefetch hits)
    if (op == TouchOp::REFILL) {
        if (dedicated_srrip(set))
            psel_counter += psel_srrip_delta;
        else if (dedicated_brrip(set))
            psel_counter -= psel_brrip_delta;
    }

    // The first hit on a prefetched line may be treated like a fill
    if (op == TouchOp::REFILL || (pf_hit_like_fill && set_contents[way].line.prefetch)) {
        Policy this_policy;

        // A small number of sets are dedicated to use the SRRIP or BRRIP policies. A fill into a dedicated SRRIP set
        // increments the policy selection counter, while a fill into a dedicated BRRIP set decrements the counter.
        // The policy for the rest of the cache is chosen using the MSB of the counter: if the counter is large then
        // SRRIP has been getting more fills and we should use BRRIP, and vice versa.
        if (is_demand && !is_nontemp) {
            if (dedicated_srrip(set)) {
                this_policy = Policy::SRRIP;
            } else if (dedicated_brrip(set)) {
                this_policy = Policy::BRRIP;
            } else {
                this_policy = psel_counter.get_bit(psel_counter_bits - 1) ? Policy::BRRIP : Policy::SRRIP;

                if (this_policy != current_policy) {
                    if (this_policy == Policy::SRRIP)
                        ev_switch_to_srrip();
                    else
                        ev_switch_to_brrip();
                    current_policy = this_policy;
                }
            }

            if (this_policy == Policy::SRRIP) {
                if (get_cycle() > 0)
                    ev_srrip_fill();

                set_age(bank, set, way, op, rrip_max - insert_delta);
            } else {
                if (get_cycle() > 0)
                    ev_brrip_fill();

                if (++brrip_counter >= brrip_long_frequency) {
                    set_age(bank, set, way, op, rrip_max - insert_delta);
                    brrip_counter = 0;
                } else {
                    set_age(bank, set, way, op, rrip_max);
                }
            }
        } else {
            // Prefetch and non-temporal fill at max age
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
