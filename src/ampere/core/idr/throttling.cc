//
// Copyright (c) 2018-2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/core/idr/throttling.h"

namespace ampere::core {

bool IDRSchedulerThrottleWatermark::is_throttling(unsigned partition) const
{
    auto count = properties.sch[Scheduler::normalized(watermark_scheduler, partition).scheduler()].get_count();
    auto &partition_throttling = throttling[partition];
    if (partition_throttling) {
        if (count < unthrottle_watermark)
            partition_throttling = false;
    } else if (count > throttle_watermark) {
        partition_throttling = true;
    }
    return partition_throttling;
}

void IDRSchedulerThrottleWatermark::adjust(std::set<SchedulerType> &eligible, unsigned partition)
{
    partition = Scheduler::normalized(watermark_scheduler, partition).partition;
    if (is_throttling(partition)) {
        if (eligible.erase(throttle_scheduler) && replace_with_watermark_scheduler)
            eligible.emplace(watermark_scheduler);
    }
}

bool IDRWidthThrottleIXUFSUThroughput::run()
{
    if (ixu_throttle->can_take() && fsu_throttle->can_take()) {
        bool status_ixu = ixu_throttle->take();
        bool status_fsu = fsu_throttle->take();

        low_issue_rate_detected[1] = low_issue_rate_detected[0];
        low_issue_rate_detected[0] = status_ixu && status_fsu;
    }

    return false;
}
} // namespace ampere::core

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
