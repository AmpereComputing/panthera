//
// Copyright (c) 2018-2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/core/idr/idr_defs.h"

namespace ampere::core {

DecodedUopGroup::DecodedUopGroup(size_t decode_lanes)
{
    for (unsigned i = 0; i < decode_lanes; i++) {
        lanes.push_back(i);
    }
}

// Switch up my style, I take any lane...
void DecodedUopGroup::switch_lanes(unsigned first, unsigned second)
{
    // Find Uop that has first and second lanes
    unsigned first_lane = 0, second_lane = 0;
    for (unsigned i = 0; i < (unsigned)lanes.size(); i++) {
        if (lanes[i] == first) {
            first_lane = i;
        } else if (lanes[i] == second) {
            second_lane = i;
        }
    }

    // Switch the lanes
    switch_current_lanes(first_lane, second_lane);
}

void DecodedUopGroup::switch_current_lanes(unsigned first, unsigned second)
{
    std::swap(lanes[first], lanes[second]);
}

} // namespace ampere::core

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
