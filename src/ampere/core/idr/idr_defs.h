//
// Copyright (c) 2018-2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include <vector>
#include <cstdint>
#include <cstddef>

#include "ampere/core/uop.h"
#include "ampere/sc/map.h"

namespace ampere::core {

struct DecodedUopGroup
{
    explicit DecodedUopGroup(size_t decode_lanes);
    void switch_lanes(unsigned first, unsigned second);
    void switch_current_lanes(unsigned first, unsigned second);

    std::vector<Uop> uops;
    std::vector<unsigned> lanes; // mapping from pre-swizzle lane to post-swizzle lane
    uint64_t lanes_used{0};
};

using SchedulerCredits = sc::sc_map<sc::CreditPool>;

} // namespace ampere::core

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
