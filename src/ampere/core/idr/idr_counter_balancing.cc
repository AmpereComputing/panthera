//
// Copyright (c) 2018-2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/core/idr/idr_counter_balancing.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <ranges>
#include <vector>
#include "ampere/archx/uop_info.h"
#include "ampere/core/ixu_defs.h"
#include "ampere/sc/counter.h"
#include "ampere/sc/report.h"
#include "ampere/util/enum.h"

namespace ampere::core {

IDRCounterBalancing::IDRCounterBalancing(unsigned width, unsigned partitions)
    : width(width),
      partitions(partitions),
      balancing_groups(util::enum_size<IDRExplicitBalancingGroups>() + bal_pipe_groups->size()),
      uop_differential(balancing_groups, SaturatingCounter(-static_cast<int>(bal_counter_size), bal_counter_size, 0)),
      balancing_stats(balancing_groups),
      balancing_min(balancing_groups),
      balancing_max(balancing_groups)
{
    auto& groups = const_cast<std::map<Uoppipe, unsigned>&>(pipe_to_group);
    for (auto [i, pipes] : std::views::enumerate(*bal_pipe_groups)) {
        for (auto pipe : pipes)
            groups.emplace(pipe, i + util::enum_size<IDRExplicitBalancingGroups>());
    }

    if (counter_balance) {
        AMPERE_ASSERT(this->partitions == SUPPORTED_PARTITIONS, "Counter balancing only works for 2 partitions");
        for (const auto& permutation : *bal_lane_perms)
            AMPERE_ASSERT(permutation.size() == width, "Lane permutation is not of decode size {}", width);
    }
}

std::optional<unsigned> IDRCounterBalancing::uop_balancing_group(unsigned index, const DecodedUopGroup& uop_group) const
{
    const auto& uops = uop_group.uops;
    const auto& uop = uops[index];

    // If two A/B/AB uops are cleaved together, then the first one goes to A pipe and the second one to B
    // (we don't allow cleaving of two A-only or two B-only uops). In this case we do not increment either
    // the A-only or B-only counter, regardless of the uops, as a timing fix in IDR.
    if (uop->is_pipe_ab()
        && ((uop->cleaved_first_half && uops[index + 1]->is_pipe_ab())
            || (uop->cleaved_second_half && uops[index - 1]->is_pipe_ab())))
        return {};

    if (auto it = pipe_to_group.find(uop->allowed_pipes()); it != pipe_to_group.end())
        return it->second;
    return {};
}

void IDRCounterBalancing::update_stats(const DecodedUopGroup& uop_group)
{
    if (!balancing_stats_collection)
        return;

    for (auto [i, uop] : std::views::enumerate(uop_group.uops) | std::views::as_const) {
        balancing_stats[static_cast<unsigned>(IDRExplicitBalancingGroups::All)][uop->decode.partition]++;
        if (auto group = uop_balancing_group(i, uop_group); group)
            balancing_stats[*group][uop->decode.partition]++;
    }

    for (auto [stats, min, max] : std::views::zip(balancing_stats, balancing_min, balancing_max)) {
        auto partition_delta = stats[0] - stats[1];
        min = std::min(min, partition_delta);
        max = std::max(max, partition_delta);
    }
}

void IDRCounterBalancing::balance(DecodedUopGroup& uop_group, const SchedulerCredits& sch)
{
    if (!counter_balance)
        return;

    // First figure out how many uops of each type are in each lane
    std::vector<std::vector<int>> per_lane(balancing_groups, std::vector<int>(width));
    std::vector<std::vector<int64_t>> counter_deltas(balancing_groups, std::vector<int64_t>(bal_lane_perms->size()));

    const auto& uops = uop_group.uops;
    for (unsigned lane_ctr = 0; auto [i, uop] : std::views::enumerate(uops)) {
        auto lane = uop_group.lanes[lane_ctr];
        if (!uop->cleaved_first_half)
            lane_ctr++;

        // For timing reasons, RTL can't count all the cleavlets of internal cleaving correctly; all that
        // is known in time is whether any uops in the instruction are internally cleaved. Since most
        // internal cleaving joins the first two uops of the instruction, we approximate by always counting
        // uop0 as two uops, and counting any later cleaved pairs as just a single uop.
        if (uop->instructions.front()->has_internal_cleaving) {
            if (uop->uop_number == 0)
                per_lane[static_cast<unsigned>(IDRExplicitBalancingGroups::All)][lane] += 2;
            else if (!uop->cleaved_second_half)
                per_lane[static_cast<unsigned>(IDRExplicitBalancingGroups::All)][lane]++;
        } else {
            per_lane[static_cast<unsigned>(IDRExplicitBalancingGroups::All)][lane]++;
        }

        // We don't know uop-type info for all uops -- by default, only the first two uops of an instruction
        bool count_uop_type = bal_info_all_uops || uop->uop_number <= 1;
        if (!count_uop_type)
            continue;

        if (auto group = uop_balancing_group(i, uop_group); group)
            per_lane[*group][lane]++;
    }

    // For each permutation, figure out what change in counter values it would cause
    for (unsigned perm = 0; perm < bal_lane_perms->size(); perm++) {
        for (uint64_t i = 0; i < width; i++) {
            auto perm_lane = (*bal_lane_perms)[perm][i];
            bool partition0 = perm_lane < (width / partitions);
            for (uint64_t bal_group = 0; bal_group < balancing_groups; bal_group++)
                counter_deltas[bal_group][perm] += partition0 ? per_lane[bal_group][i] : -per_lane[bal_group][i];
        }
    }

    std::vector<int> perm_votes(bal_lane_perms->size(), 0);

    // Now each counter gets a chance to vote on each permutation
    for (auto [bal_group, uop_diff] : std::views::enumerate(uop_differential)) {
        auto ctr_val = uop_diff.get_value();
        for (auto [perm, votes] : std::views::enumerate(perm_votes)) {
            auto ctr_delta = counter_deltas[bal_group][perm];

            if (ctr_val > 0) {
                // If the counter is positive, vote for anything that does not make it more positive
                if (ctr_delta <= 0)
                    votes++;
            } else if (ctr_val < 0) {
                // If the counter is negative, vote for anything that does not make it more negative
                if (ctr_delta >= 0)
                    votes++;
            } else {
                // If the counter is zero we vote for anything which moves the counter as little
                // as possible in either direction. First find the minimum possible delta.
                int64_t min_abs_delta = std::abs(counter_deltas[bal_group][0]);
                for (unsigned i = 1; i < bal_lane_perms->size(); i++)
                    min_abs_delta = std::min(min_abs_delta, std::abs(counter_deltas[bal_group][i]));

                // Now vote for anything which matches the minimum
                if (std::abs(ctr_delta) == min_abs_delta)
                    votes++;
            }
        }
    }

    // See which permutation got the most votes -- ties are broken by round-robin
    int most_votes{-1};
    int best_perm{-1};
    if (bal_round_robin && ++perm_round_robin == bal_lane_perms->size())
        perm_round_robin = 0;
    for (unsigned pcount = 0; pcount < bal_lane_perms->size(); pcount++) {
        auto perm = (pcount + perm_round_robin) % bal_lane_perms->size();
        if (perm_votes[perm] > most_votes) {
            most_votes = perm_votes[perm];
            best_perm = perm;
        }
    }

    // Swizzle the lanes according to the chosen permutation
    // uop_group.lanes array means uop[n] gets partition/lane lanes[n]
    AMPERE_ASSERT(best_perm != -1, "Counter balancing failed to choose a permutation");
    event.swizzled(uop_group.uops[0], event.swizzle_index(best_perm));
    for (uint64_t i = 0; i < width; i++)
        uop_group.lanes[i] = (*bal_lane_perms)[best_perm][uop_group.lanes[i]];

    // Update the counters to reflect the effects of the uops we just assigned
    // Counters go up for uops sent to partition 0, and down for uops sent to partition 1
    // Accumulate the delta from all lanes and then apply it to the counter, to avoid saturating intermediate results
    for (auto [bal_group, uop_diff] : std::views::enumerate(uop_differential)) {
        int64_t delta{0};
        for (uint64_t from_lane = 0; from_lane < width; from_lane++) {
            auto to_lane = uop_group.lanes[from_lane];
            bool partition0 = to_lane < (width / partitions);
            delta += partition0 ? per_lane[bal_group][from_lane] : -per_lane[bal_group][from_lane];
        }

        uop_diff += delta;
    }

    // Tweak the counters to bias balancing toward schedulers which have more room
    for (auto sched : *bal_sch_occupancy) {
        int credits[SUPPORTED_PARTITIONS];
        for (auto [p, pc] : std::views::enumerate(credits))
            credits[p] = sch[Scheduler(sched, p).scheduler()].get_count(); // uint64_t -> int conversion!

        auto diff = credits[0] - credits[1];
        auto abs_diff = std::abs(diff);

        if (diff != 0 && abs_diff >= static_cast<int>(bal_occupancy_thresh)) {
            int incr = (diff > 0)
                           ? -1 // P0 has more space, so make the counters more negative so that we send things to P0
                           : 1; // P1 has more space, so make the counters more positive so that we send things to P1

            for (auto [pipe, group] : pipe_to_group) {
                if (pipe_from_sch(sched, pipe) != Pipe::INVALID)
                    uop_differential[group] += incr;
            }
        }
    }
}

void IDRCounterBalancing::flush(bool non_speculative)
{
    if (bal_clear_on_nonspec_flush && non_speculative) {
        for (auto& counter : uop_differential)
            counter.set_value(0);
    }
}

void IDRCounterBalancing::dump(std::ostream& os) const
{
    if (!balancing_stats_collection)
        return;

    for (uint64_t b = 0; b < balancing_groups; b++) {
        auto group =
            b < util::enum_size<IDRExplicitBalancingGroups>()
                ? util::enum_to_string(static_cast<IDRExplicitBalancingGroups>(b))
                : util::enum_to_string(bal_pipe_groups->at(b - util::enum_size<IDRExplicitBalancingGroups>())[0]);
        fmt::print(os,
                   "BALANCE_STATS: P0_{} {} P1_{} {} delta {}\n",
                   group,
                   balancing_stats[b][0],
                   group,
                   balancing_stats[b][1],
                   std::abs(balancing_stats[b][0] - balancing_stats[b][1]));
    }

    fmt::print(os, "BALANCE_STATS: min-max ");
    for (uint64_t b = 0; b < balancing_groups; b++) {
        auto group =
            b < util::enum_size<IDRExplicitBalancingGroups>()
                ? util::enum_to_string(static_cast<IDRExplicitBalancingGroups>(b))
                : util::enum_to_string(bal_pipe_groups->at(b - util::enum_size<IDRExplicitBalancingGroups>())[0]);
        fmt::print(os, "{} {} {}  ", group, balancing_min[b], balancing_max[b]);
    }
    fmt::print(os, "\n");

    fmt::print(os, "BALANCE_STATS: graph");
    for (uint64_t b = 0; b < balancing_groups; b++) {
        fmt::print(
            os, " {}", std::abs(balancing_min[b]) > std::abs(balancing_max[b]) ? balancing_min[b] : balancing_max[b]);
        fmt::print(os, "\n");
    }
}

} // namespace ampere::core

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
