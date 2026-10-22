//
// Copyright (c) 2018-2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include "ampere/core/idr/idr_defs.h"
#include "ampere/core/ixu_defs.h"
#include "ampere/sc/param.h"

using sc::Param;

namespace ampere::core {

class IDRLoadBalancing
{
    unsigned width;
    unsigned partitions = MAX_PARTITIONS;
    unsigned lower_uops_partition{0}; // This signal is used to load balance of size 1, this is actually done in a
                                      // earilier stage of the IDR so it is seperate from other ld balance signal
    unsigned upper_uops_partition{0}; // Signal that indicates when we need to load balance

    Param<bool> load_balance{"load_balance", "Balance Uop Lanes to different partitions ping pong", true};

    unsigned partition_size() const { return width / partitions; }
    unsigned start_of_last_partition() const { return partition_size() * (partitions - 1); }

    void distribute(DecodedUopGroup& uop_group, unsigned offset = 0) const;
    void distribute_one(DecodedUopGroup& uop_group, unsigned offset = 0) const;

public:
    explicit IDRLoadBalancing(unsigned width, unsigned partitions = MAX_PARTITIONS)
        : width(width), partitions(partitions)
    {
        assert(!load_balance || (partitions > 1 && width % partitions == 0));
    }
    explicit operator bool() const { return load_balance; }
    bool balance(DecodedUopGroup& uop_group);
    bool history(DecodedUopGroup& uop_group) const;
};

} // namespace ampere::core

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
