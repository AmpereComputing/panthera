//
// Copyright (c) 2018-2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/core/idr/swizzle.h"

#include <array>
#include "ampere/util/enum.h"

namespace ampere::core {

std::string Swizzle::swizzle_uops(DecodedUopGroup& uops,
                                  const IDRLoadBalancing& load_balancing,
                                  const SchedulerCredits& sch)
{
    if (!swizzle)
        return "Disabled";

    unsigned mem_uops = 0;
    auto group_size = static_cast<unsigned>(uops.lanes_used);
    std::string swizzle_str = "Type: ";

    // Generate uop key for lookup table
    for (unsigned i = 0, lane = 0; i < uops.uops.size(); i++) {
        Uop uop = uops.uops[i];
        if (uop->cleaved_second_half) {
            AMPERE_ASSERT(!(uop->is_load() || uop->is_ixu_store()), "Second half of cleaved pair was was a memory uop");
            continue;
        }
        if (uop->is_load() || uop->is_ixu_store()) {
            mem_uops += (1 << uops.lanes[lane]);
        }
        lane++;
    }

    // Load balance history has already been toggled by load balancer
    bool load_balance_history = load_balancing.history(uops);

    // Lookup swizzle type
    // load_balance_history == true means load balancing gave more uops to partition 1
    auto so = swizzle_table->get({group_size, mem_uops, load_balance_history, swizzle_history});

    // This is an ugly hack to get better balancing in the bcopy loop. If we have four memory uops, then
    // look at the two memory schedulers and do a special swizzle based on which of them is least loaded.
    // On average this is just random noise, but for the specific uop pattern of bcopy it ends up
    // balancing the store uops perfectly between the schedulers.
    if (bcopy_swizzle_tweak && group_size == 4 && mem_uops == 0xf) {
        std::array<unsigned, IXU_PARTITIONS> memory_sch_count;
        for (unsigned i = 0; i < IXU_PARTITIONS; ++i)
            memory_sch_count[i] = sch[Scheduler(Sch::M, i).scheduler()].get_count();

        if (memory_sch_count[0] < memory_sch_count[1]) {
            so.type = SwizzleType::ONETHREE;
        } else if (memory_sch_count[0] > memory_sch_count[1]) {
            so.type = SwizzleType::ZEROTWO;
        }
    }

    swizzle_str += swizzle_uops(uops, so.type);
    swizzle_str += ", SwizHistory: " + std::to_string(swizzle_history);

    if (so.set_swizzle_history) {
        swizzle_history = so.swizzle_history;
        swizzle_str += ", Set SwizHistory: " + std::to_string(so.swizzle_history);
    }

    return swizzle_str;
}

std::string_view Swizzle::swizzle_uops(DecodedUopGroup& uops, SwizzleType type)
{
    switch (type) {
    case SwizzleType::FULL:
        uops.switch_lanes(1, 3);
        uops.switch_lanes(0, 2);
        break;
    case SwizzleType::MIDDLE:
        uops.switch_lanes(1, 2);
        break;
    case SwizzleType::ZEROTWO:
        uops.switch_lanes(0, 2);
        break;
    case SwizzleType::ONETHREE:
        uops.switch_lanes(1, 3);
        break;
    default:
        break;
    }

    return util::enum_to_string(type);
}

} // namespace ampere::core

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
