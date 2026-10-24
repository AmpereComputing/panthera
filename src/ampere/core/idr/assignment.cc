//
// Copyright (c) 2018-2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/core/idr/assignment.h"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <ranges>
#include <vector>
#include "ampere/core/ixu_defs.h"

namespace ampere::core {

unsigned AssignmentProperties::partition_of(const ConstUop& uop) const
{
    return uses_pre_swizzle_partition ? uop->decode.pre_swizzle_partition : uop->decode.partition;
}

unsigned AssignmentProperties::lane_of(const ConstUop& uop) const
{
    return uses_pre_swizzle_partition ? uop->decode.pre_swizzle_lane : uop->decode.lane;
}

Scheduler LeastLoadedScheduler::assign(const Uop& uop)
{
    auto partition = properties.partition_of(uop);
    auto eligible = Scheduler::eligible_schedulers(uop->allowed_pipes());
    std::vector<size_t> counts(properties.sch.size(), 0);

    for (auto [s, scheduler] : std::views::enumerate(eligible) | std::views::as_const) {
        auto sched = scheduler.scheduler();
        counts[s] = properties.sch[sched].get_count();

        // Take into account uops which have already been assigned a scheduler in this same cycle,
        // and also avoid sending more uops than allowed to a single scheduler. Without smart_least_loaded,
        // if pipe balancing is disabled then all uops will go to the scheduler which had the most
        // credits at the beginning of the cycle.
        if (update_within_cycle) {
            counts[s] -= sch_assigned_this_cycle[sched];

            // We can send 2 uops to the IXU schedulers and 4 to FSU
            unsigned max_uops_to_sch = properties.width / properties.partitions;
            if (sch_assigned_this_cycle[sched] >= max_uops_to_sch)
                counts[s] = 0;
        }
    }

    bool all_credits_equal{true};
    for (unsigned s = 1; s < eligible.size(); s++) {
        if (counts[s] != counts[0]) {
            all_credits_equal = false;
            break;
        }
    }

    // Special case for a uop which can go to two schedulers, and they have an equal number of
    // credits: the pipe depends on which partition/lane this uop is in, in ABBA/XYYX pattern.
    // Later this was simplified to just go to a default pipe
    if (eligible.size() > 1 && all_credits_equal) {
        if (default_scheduler)
            return Scheduler::normalized(*default_scheduler, partition);

        auto lane = properties.lane_of(uop);
        return eligible[(partition + lane) % eligible.size()];
    }

    // If the credits aren't tied, then we pick the least loaded scheduler
    auto first = eligible[0];
    auto least_loaded{first};
    auto max_credits = counts[0];
    for (unsigned i = 1; i < eligible.size(); ++i) {
        auto credits = counts[i];
        if (credits > max_credits) {
            max_credits = credits;
            least_loaded = eligible[i];
        }
    }

    return least_loaded;
}

void MultipleIDRSchedulerAssignment::stalled()
{
    for (auto& assignment : assignments)
        assignment->stalled();
}

void MultipleIDRSchedulerAssignment::start_assignment()
{
    for (auto& assignment : assignments)
        assignment->start_assignment();
}

std::vector<IDRSchedulerAssignment::assignment_t> MultipleIDRSchedulerAssignment::assign_uops(
    const std::vector<Uop>& uops)
{
    std::vector<IDRSchedulerAssignment::assignment_t> result(uops.size());
    for (auto& assignment : assignments) {
        auto assign_result = assignment->assign_uops(uops);

        unsigned assigned = 0;
        for (auto [r, ra] : std::views::zip(result, assign_result)) {
            if (!r)
                ra = r;
            assigned += static_cast<bool>(r);
        }

        if (assigned == result.size())
            break;
    }

    return result;
}

std::vector<IDRSchedulerAssignment::assignment_t> BalancingIDRSchedulerAssignment::assign_uops(
    const std::vector<Uop>& uops)
{
    if (uops.empty())
        return {};

    auto partition = partition_of(uops[0]);
    std::vector<std::pair<size_t, std::set<SchedulerType>>> all_uops;
    all_uops.reserve(uops.size());

    size_t index = 0;
    for (const auto& uop : uops)
        all_uops.emplace_back(++index, Scheduler::eligible_scheduler_types(uop->allowed_pipes()));

    std::sort(all_uops.begin(), all_uops.end(), [](const auto& a, const auto& b) {
        return a.second.size() < b.second.size();
    });

    std::vector<assignment_t> picked;
    std::set<SchedulerType> used;
    for (auto& [index, eligible] : all_uops) {
        for (auto sch : used)
            eligible.erase(sch);

        AMPERE_ASSERT(eligible.size() >= 1, "No bindings");
        SchedulerType bind = *eligible.begin();
        if (eligible.size() > 1) {
            std::vector<std::pair<SchedulerType, unsigned>> counts;
            for (const auto& s : eligible)
                counts.emplace_back(s, properties.sch[Scheduler::normalized(s, partition).scheduler()].get_count());

            std::sort(counts.begin(), counts.end(), [](const auto& a, const auto& b) { return a.second < b.second; });

            auto threshold = counts.back().second - counts.front().second;
            if (threshold >= balancing_threshold) {
                bind = counts.back().first;
            }
        }

        used.emplace(bind);
        picked[index] = Scheduler::normalized(bind, partition);
    }

    return picked;
}

void BinaryAssignment::update_credits_for_partition(unsigned partition)
{
    first_scheduler = Scheduler::normalized(first_scheduler_type, partition);
    second_scheduler = Scheduler::normalized(second_scheduler_type, partition);

    first_available = properties.sch[first_scheduler.scheduler()].get_count();
    second_available = properties.sch[second_scheduler.scheduler()].get_count();
}

bool BinaryAssignment::first_has_more_credits() const
{
    return ((first_balancing_threshold && first_available > second_available
             && (first_available - second_available) > first_balancing_threshold)
            || (avoid_full && second_available == 0));
}

bool BinaryAssignment::second_has_more_credits() const
{
    return ((second_balancing_threshold && second_available > first_available
             && (second_available - first_available) > second_balancing_threshold)
            || (avoid_full && first_available == 0));
}

IDRSchedulerAssignment::assignment_t BinaryAssignment::assign_uop(const Uop& uop)
{
    auto eligible = Scheduler::eligible_scheduler_types(uop->allowed_pipes());
    bool uop_first_only = eligible.size() == 1 && eligible.contains(*first_scheduler_type);
    bool uop_second_only = eligible.size() == 1 && eligible.contains(*second_scheduler_type);

    // uops which can only go to one pipe go there
    // for uops which can go to both pipes,
    //    if A has a lot more credits then go to A,
    //    else if B has a lot more credits then go to B

    // Decide where uop should go
    if (uop_first_only)
        return first_scheduler; // must go to A
    if (uop_second_only)
        return second_scheduler; // must go to B
    if (first_has_more_credits())
        return first_scheduler; // A has a lot more credits
    if (second_has_more_credits())
        return second_scheduler; // B has a lot more credits

    return {};
}

IDRSchedulerAssignment::assignment_t BinaryAssignment::fallback_uop_assignment(unsigned index,
                                                                               const Uop& uop,
                                                                               const assignment_t& neighbor)
{
    if (neighbor) {
        // if uop has a neighbor which can only go to A/B then go to B/A
        return neighbor->sched_type == first_scheduler_type ? second_scheduler : first_scheduler;
    }

    // else if uop has a neighbor which can go to either pipe then uop0 to A and uop1 to B
    return (index & 1) ? second_scheduler : first_scheduler;
}

std::vector<IDRSchedulerAssignment::assignment_t> BinaryAssignment::assign_uops(const std::vector<Uop>& uops)
{
    if (uops.empty())
        return {};

    // Only balance if more than 1 uop
    if (uops.size() == 1)
        return {single_uop_assignment(uops[0])};

    auto partition = partition_of(uops[0]);
    for (const auto& uop : uops) {
        AMPERE_ASSERT(partition_of(uop) == partition, "IDR: uops not in same partition");
    }

    update_credits_for_partition(partition);

    std::vector<assignment_t> initial_assignment;
    initial_assignment.reserve(uops.size());

    // Assign uops only based upon individual uop
    for (const auto& uop : uops)
        initial_assignment.emplace_back(assign_uop(uop));

    // Assign uops based upon previous decision and neighboring uop
    auto final_assignment = initial_assignment;
    for (auto [i, uop, sched] : std::views::zip(std::views::iota(0), uops, initial_assignment)) {
        if (!sched) {
            auto default_index = (use_partition_for_default ? partition : 0) + i;
            final_assignment[i] =
                fallback_uop_assignment(default_index, uop, initial_assignment[(i + 1) % initial_assignment.size()]);
        }
    }

    return final_assignment;
}

IDRSchedulerAssignment::assignment_t BinaryPingPongIDRSchedulerAssignment::single_uop_assignment(const Uop& uop)
{
    if (use_ping_pong(uop)) {
        auto partition = partition_of(uop);
        update_credits_for_partition(partition);
        if (first_has_more_credits())
            return first_scheduler;
        if (second_has_more_credits())
            return second_scheduler;
        return Scheduler::normalized(ping_pong.next_scheduler(*first_scheduler_type, *second_scheduler_type, partition),
                                     partition);
    }

    return BinaryAssignment::single_uop_assignment(uop);
}

IDRSchedulerAssignment::assignment_t BinaryPingPongIDRSchedulerAssignment::fallback_uop_assignment(
    unsigned index, const Uop& uop, const assignment_t& neighbor)
{
    if (use_ping_pong(uop)) {
        auto partition = partition_of(uop);
        return Scheduler::normalized(ping_pong.next_scheduler(*first_scheduler_type, *second_scheduler_type, partition),
                                     partition);
    }

    return BinaryAssignment::fallback_uop_assignment(index, uop, neighbor);
}

IDRSchedulerAssignment::assignment_t PingPongAssignment::assign_uop(const Uop& uop)
{
    if (uop->allowed_pipes() == *pipe) {
        auto partition = partition_of(uop);
        return Scheduler::normalized(ping_pong.next_scheduler(*first_scheduler_type, *second_scheduler_type, partition),
                                     partition);
    }

    return {};
}

IDRSchedulerAssignment::assignment_t FSUUnitPingPongAssignment::assign_uop(const Uop& uop)
{
    auto eligible = Scheduler::eligible_scheduler_types(uop->allowed_pipes());
    if (uop->fsu_unit() == *unit && eligible.contains(*first_scheduler_type)
        && eligible.contains(*second_scheduler_type))
    {
        auto partition = partition_of(uop);
        return Scheduler::normalized(ping_pong.next_scheduler(*first_scheduler_type, *second_scheduler_type, partition),
                                     partition);
    }

    return {};
}

} // namespace ampere::core

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
