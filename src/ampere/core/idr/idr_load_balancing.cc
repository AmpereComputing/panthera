//
// Copyright (c) 2018-2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/core/idr/idr_load_balancing.h"

namespace ampere::core {

void IDRLoadBalancing::distribute(DecodedUopGroup& uop_group, unsigned offset) const
{
    auto lanes = uop_group.lanes_used;
    unsigned per = lanes / partitions;
    for (unsigned i = 0, from = lanes - 1, to = start_of_last_partition(); i < partitions - 1;
         ++i, to -= partition_size())
        for (unsigned j = 0, extra = offset && i + 1 == offset ? lanes % partitions : 0; j < per + extra; ++j, --from)
            uop_group.switch_current_lanes(from, to + per + extra - 1 - j);
}

void IDRLoadBalancing::distribute_one(DecodedUopGroup& uop_group, unsigned offset) const
{
    for (unsigned i = 0, p = offset; i < uop_group.lanes_used; ++i, p = (p + 1) % partitions)
        uop_group.switch_current_lanes(i, partition_size() * p);
}

bool IDRLoadBalancing::balance(DecodedUopGroup& uop_group)
{
    auto lanes = uop_group.lanes_used;
    if (!load_balance || !lanes || lanes == width)
        return false;

    // Perform load balancing on lanes
    bool did_ld_balance{false};

    if (lanes > 1 && uop_group.uops[1]->archx_uop->ldp_2nd_uop) {
        // LDPs can't have a gap between the lanes
        if (lanes < partitions)
            lower_uops_partition = 0;
        else if (lanes > partitions)
            upper_uops_partition = 0;
    } else if (lanes < partitions) {
        // Lanes less than partitions alternate uops into different partitions
        if (lower_uops_partition != 0 || lanes > 1) {
            distribute_one(uop_group, lower_uops_partition);
            did_ld_balance = true;
        }

        if (++lower_uops_partition == partitions)
            lower_uops_partition = 0;
    } else if (lanes % partitions == 0) {
        // Lanes some multiple of partitions just distribute evenly
        distribute(uop_group);
        did_ld_balance = true;
    } else if (lanes <= start_of_last_partition()) {
        // Lanes less than all partition minimum require swapping some into empty partitions
        distribute(uop_group, upper_uops_partition);
        did_ld_balance = true;

        if (++upper_uops_partition == partitions)
            upper_uops_partition = 0;
    } else if (lanes < width) {
        // Lanes more than partitions alternate distribution
        if (upper_uops_partition != 0) {
            unsigned distance = width > 4 ? 1 : 0; // for backwards compatability
            unsigned to = start_of_last_partition() + ((lanes - 1) / partitions);
            for (unsigned from = lanes - 1; from >= partition_size() * (partitions - upper_uops_partition) - 1;
                 --from, to -= distance)
                uop_group.switch_current_lanes(from, to);

            did_ld_balance = true;
        }

        if (++upper_uops_partition == partitions)
            upper_uops_partition = 0;
    }

    return did_ld_balance;
}

bool IDRLoadBalancing::history(DecodedUopGroup& uop_group) const
{
    if (!load_balance || uop_group.lanes_used == width)
        return false;

    if (uop_group.lanes_used < partitions)
        return lower_uops_partition == 0;
    if (uop_group.lanes_used % partitions == 0 || uop_group.lanes_used <= start_of_last_partition())
        return true;

    return upper_uops_partition == 0;
}

} // namespace ampere::core

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
