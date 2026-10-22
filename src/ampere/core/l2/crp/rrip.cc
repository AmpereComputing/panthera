//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/core/l2/crp/rrip.h"

namespace ampere::core {

void L2RRIP::init(uint64_t _banks, uint64_t _sets, uint64_t _ways, uint64_t _ways_mask)
{
    banks = _banks;
    sets = _sets;
    ways = _ways;
    ways_mask = _ways_mask;

    start_ptr.resize(banks, 0);
    age.resize(banks);
    stats.resize(banks);

    for (uint64_t b = 0; b < banks; b++) {
        age[b].resize(sets);
        stats[b].resize(sets);
        for (uint64_t s = 0; s < sets; s++) {
            age[b][s].resize(ways, 0);
            stats[b][s].resize(ways);
        }
    }
}

void L2RRIP::set_age(uint64_t bank, uint64_t set, uint64_t way, TouchOp op, int new_age)
{
    age[bank][set][way] = new_age;

    // Prediction is only updated on fills. If we are filling this line at max age, then we are predicting
    // that it will not be reused.
    if (op == TouchOp::REFILL)
        stats[bank][set][way].reuse_prediction = (new_age != rrip_max);
}

void L2RRIP::touch_stats(uint64_t bank, uint64_t set, uint64_t way, TouchOp op, bool demand, MemReq req)
{
    auto& lstats = stats[bank][set][way];

    // First track basic stats about what fraction of demand/prefetch fills are hit
    if (op == TouchOp::REFILL) {
        if (demand)
            ev_crp_demand_fill();
        else
            ev_crp_prefetch_fill();
    } else if (!lstats.hit_since_filled) {
        if (lstats.origin == MemOp::Requester::PREFETCH)
            ev_crp_prefetch_hit();
        else if (lstats.origin != MemOp::Requester::NONE)
            ev_crp_demand_hit();
    }

    // We only want to capture prediction statistics for lines which:
    //   1) are being hit for the first time since they were filled --> hit_since_filled == false (but will be
    //      set to true at the end of this function)
    //   2) are being evicted without ever having been hit --> hit_since_filled == false
    if (lstats.valid && !lstats.hit_since_filled) {
        switch (lstats.origin) {
        case MemOp::Requester::ICF:
            if (op == TouchOp::REFILL) {
                if (lstats.reuse_prediction)
                    ev_crp_code_reuse_mispred(req); // evicted line was not hit, predicted reuse -> reuse mispredict
                else
                    ev_crp_code_no_reuse_correct(
                        req); // evicted line was not hit, predicted no reuse -> no reuse correct prediction
            } else {
                if (lstats.reuse_prediction)
                    ev_crp_code_reuse_correct(req); // first hit on line, predicted reuse -> reuse correct
                else
                    ev_crp_code_no_reuse_mispred(req); // first hit on line, predicted no reuse -> no reuse mispredict
            }
            break;

        case MemOp::Requester::LSU:
            if (op == TouchOp::REFILL) {
                if (lstats.reuse_prediction)
                    ev_crp_data_reuse_mispred(req); // evicted line was not hit, predicted reuse -> reuse mispredict
                else
                    ev_crp_data_no_reuse_correct(
                        req); // evicted line was not hit, predicted no reuse -> no reuse correct prediction
            } else {
                if (lstats.reuse_prediction)
                    ev_crp_data_reuse_correct(req); // first hit on line, predicted reuse -> reuse correct
                else
                    ev_crp_data_no_reuse_mispred(req); // first hit on line, predicted no reuse -> no reuse mispredict
            }
            break;

        case MemOp::Requester::IMMU:
        case MemOp::Requester::DMMU:
            if (op == TouchOp::REFILL) {
                if (lstats.reuse_prediction)
                    ev_crp_mmu_reuse_mispred(req); // evicted line was not hit, predicted reuse -> reuse mispredict
                else
                    ev_crp_mmu_no_reuse_correct(
                        req); // evicted line was not hit, predicted no reuse -> no reuse correct prediction
            } else {
                if (lstats.reuse_prediction)
                    ev_crp_mmu_reuse_correct(req); // first hit on line, predicted reuse -> reuse correct
                else
                    ev_crp_mmu_no_reuse_mispred(req); // first hit on line, predicted no reuse -> no reuse mispredict
            }
            break;

        default:
            // For any other requesters we don't log any prediction events
            break;
        }
    }

    // update stats tracking
    if (op == TouchOp::REFILL) {
        // reuse_prediction updated in touch() once we know what we're doing with this line
        lstats.valid = true;
        lstats.hit_since_filled = false;
        if (req)
            lstats.origin = req->origin;
        else
            lstats.origin = MemOp::Requester::NONE;
    } else {
        // hit doesn't change the origin or prediction, which are decided at fill time
        lstats.hit_since_filled = true;
    }
}

// FIXME: should this function take ways_mask into account?
void L2RRIP::touch(
    uint64_t bank, uint64_t set, uint64_t way, TouchOp op, bool demand, const Set& set_contents, MemReq req)
{
    touch_stats(bank, set, way, op, demand, req);

    bool is_demand = demand || prefetch_fills_act_like_demand;
    bool is_nontemp = req && req->nontemporal;

    // The first hit on a prefetched line may be treated like a fill
    if (op == TouchOp::REFILL || (pf_hit_like_fill && set_contents[way].line.prefetch)) {
        // Prefetches and non-temporals fill at max age, demand at (max - delta)
        if (is_demand && !is_nontemp)
            set_age(bank, set, way, op, rrip_max - insert_delta);
        else
            set_age(bank, set, way, op, rrip_max);
    } else if (op == TouchOp::LOOKUP && !is_nontemp) {
        // Non-temporal hits don't update the age
        set_age(bank, set, way, op, 0);
    } else if ((op == TouchOp::UPDATE && write_hit_updates_age) && !is_nontemp) {
        set_age(bank, set, way, op, 0);
    }
}

uint64_t L2RRIP::inc_mod_ways(uint64_t x)
{
    ++x;
    return x < ways ? x : 0;
}

// Choose a victim for replacement
uint64_t L2RRIP::victim(uint64_t bank, uint64_t set, const Set& set_contents, uint64_t way_mask)
{
    start_ptr[bank] = inc_mod_ways(start_ptr[bank]);

    uint64_t max_way{ways}; // initialize to illegal value for assertion check below
    int max_age{-1};

    for (uint64_t i = 0, w = use_round_robin ? start_ptr[bank] : 0; i < ways; ++i, w = inc_mod_ways(w)) {
        auto& line = set_contents[w].line;

        // Skip this way if it is masked out
        if (((1ul << w) & way_mask) == 0)
            continue;

        // Skip this way if it is busy
        if (line.busy)
            continue;

        // An invalid line is always chosen first (if it's not busy)
        if (line.state == LineState::I)
            return w;

        // If we are paying attention to the L1-present bits, then we treat any line in an L1 as though it had age 0.
        int effective_age;
        if ((get_l1i(line) && !ignore_in_l1i) || (get_l1d(line) && !ignore_in_l1d))
            effective_age = 0;
        else
            effective_age = age[bank][set][w];

        // Age is the top priority -- always pick a line with higher age
        if (effective_age > max_age) {
            max_age = effective_age;
            max_way = w;
        } else if (effective_age == max_age) {
            // If ages are equal, then maybe we prefer a line with its prefetch bit set, or a line which is not in an
            // L1. Note that a line cannot be both prefetched and in an L1, since only a demand load can set the in-L1
            // bit, and demand hits clear the prefetched bit.
            if (prefetch_priority && line.prefetch && !set_contents[max_way].line.prefetch)
                max_way = w;
            if (l1_priority && !(get_l1i(line) || get_l1d(line))
                && (get_l1i(set_contents[max_way].line) || get_l1d(set_contents[max_way].line)))
                max_way = w;

            // We may also prefer to evict a dirty line over a clean one
            if (dirty_tiebreak && is_dirty(line.state) && !is_dirty(set_contents[max_way].line.state))
                max_way = w;
        }
    }

    // Age all lines so that the chosen victim has the max age
    if (max_age < rrip_max) {
        auto increment = rrip_max - max_age;
        for (uint64_t w = 0; w < ways; ++w)
            age[bank][set][w] = std::min(age[bank][set][w] + increment, rrip_max.val());
    }

    AMPERE_ASSERT(max_way < ways, "Failed to choose a valid victim");
    ev_crp_evict();
    return max_way;
}

void L2RRIP::invalidate(uint64_t bank, uint64_t set, uint64_t way)
{
    age[bank][set][way] = rrip_max;
}

} // namespace ampere::core
