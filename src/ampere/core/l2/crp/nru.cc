//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/core/l2/crp/nru.h"

namespace ampere::core {

void NRU::init(uint64_t _banks, uint64_t _sets, uint64_t _ways, uint64_t _ways_mask)
{
    banks = _banks;
    sets = _sets;
    ways = _ways;
    ways_mask = _ways_mask;

    start_ptr.resize(banks, 0);
    r_value.resize(banks);

    for (uint64_t b = 0; b < banks; b++) {
        r_value[b].resize(sets);
        for (uint64_t s = 0; s < sets; s++)
            r_value[b][s].resize(ways, false);
    }
}

void NRU::touch(uint64_t bank, uint64_t set, uint64_t way, TouchOp op, bool demand, const Set& set_contents, MemReq req)
{
    // FIXME: should this function take ways_mask into account?

    // On any access, if all other ways are recent (i.e. if this access might make all lines recent),
    // then we clear all recent bits. Then, if this was a demand access, we set the recent bit for
    // the just-accessed way; if this is a prefetch, then we don't. This means that after a demand
    // access there is always at least one recent bit set, but prefetches may leave no recent bits set.

    if ((op == TouchOp::UPDATE || op == TouchOp::LOOKUP) && !demand)
        AMPERE_ERROR("L2 NRU: lookups and updates should always be demand accesses");

    // First set (maybe temporarily) recent for this way, and count the recent bits
    r_value[bank][set][way] = true;
    uint64_t count_recent{0};
    for (uint64_t w = 0; w < ways; w++)
        if (r_value[bank][set][w])
            ++count_recent;

    // If all lines might be recent after this access, clear all recent bits
    if (count_recent == ways)
        for (uint64_t w = 0; w < ways; w++)
            r_value[bank][set][w] = false;

    // Now set the recent bit for the accessed way according to whether this was a prefetch or demand access
    r_value[bank][set][way] = (demand || prefetch_fills_act_like_demand);
}

int NRU::pick_way(uint64_t bank, unsigned victim)
{
    uint64_t way = start_ptr[bank];

    do {
        if (victim & (1 << way))
            return way;

        way = (way + 1) % ways;
    } while (way != start_ptr[bank]);

    return 0;
}

// Choose a victim for replacement
uint64_t NRU::victim(uint64_t bank, uint64_t set, const Set& set_contents, uint64_t way_mask)
{
    enum {
        INVALID,
        NOT_IN_L1_NOT_RECENT_PREFETCH,
        NOT_IN_L1_NOT_RECENT,
        NOT_IN_L1,
        NOT_RECENT,
        OTHER,
        NUM_PRIORITIES
    };

    std::vector<unsigned> way_bits(NUM_PRIORITIES, 0);

    for (auto w = 0u, wbit = 1u; w < ways; ++w, wbit <<= 1) {
        if (wbit & way_mask) {
            auto& way = set_contents[w];
            auto& line = way.line;

            if (line.busy)
                continue;

            if (line.state == LineState::I) {
                way_bits[INVALID] |= wbit;
            } else {
                way_bits[OTHER] |= wbit;
                if (!r_value[bank][set][w]) {
                    way_bits[NOT_RECENT] |= wbit;
                    if (!get_l1d(line) && !get_l1i(line)) {
                        way_bits[NOT_IN_L1_NOT_RECENT] |= wbit;
                        if (line.prefetch)
                            way_bits[NOT_IN_L1_NOT_RECENT_PREFETCH] |= wbit;
                    }
                } else if (!get_l1d(line) && !get_l1i(line)) {
                    way_bits[NOT_IN_L1] |= wbit;
                }
            }
        }
    }

    start_ptr[bank] = (start_ptr[bank] + 1) % ways;

    for (auto victim : way_bits) {
        if (victim)
            return pick_way(bank, victim);
    }

    AMPERE_ERROR("failed to choose a victim way");
    return 0;
}

void NRU::invalidate(uint64_t bank, uint64_t set, uint64_t way)
{
    r_value[bank][set][way] = false;
}

} // namespace ampere::core
