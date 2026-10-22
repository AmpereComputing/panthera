//
// Copyright (c) 2018-2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include "ampere/archx/uop_info.h"
#include "ampere/core/idr/idr_defs.h"
#include "ampere/core/ixu_defs.h"
#include "ampere/sc/param.h"
#include "ampere/sc/counter.h"

using sc::Event;
using sc::EventData;
using sc::Param;
using sc::SaturatingCounter;

make_ns_enum(ampere::core, IDRExplicitBalancingGroups,
    All
);

namespace ampere::core {

class IDRCounterBalancing
{
    inline static constexpr unsigned SUPPORTED_PARTITIONS = 2;

    unsigned width;
    unsigned partitions;

    // The array size is hard-coded because the counter-balancing algorithm only works for 4 lanes, and there are
    // 4 permutations of 4 elements. The order of permutations matters, as earlier ones are attempted first.
    static inline const unsigned default_lane_perms[4][4] = {
        {0, 1, 2, 3}, // No swizzle
        {0, 3, 2, 1}, // One-three swizzle
        {2, 1, 0, 3}, // Zero-two swizzle
        {2, 3, 0, 1}  // Full swizzle
    };
    unsigned perm_round_robin{0};

    Param<bool> counter_balance{"counter_balance", "Counter-based partition balancing", false};
    Param<std::vector<std::vector<archx::Uoppipe>>> bal_pipe_groups{
        "bal_pipe_groups",
        "Groups of pipes to balance",
        {{Uoppipe::C}, {Uoppipe::D}, {Uoppipe::A}, {Uoppipe::B, Uoppipe::BS}}};
    Param<std::vector<std::vector<unsigned>>> bal_lane_perms{
        "bal_lane_perms", "Permutations to consider when balancing", default_lane_perms};
    Param<unsigned> bal_counter_size{"bal_counter_size", "Balancing counters range from -N to +N", 7};
    Param<bool> bal_info_all_uops{
        "bal_info_all_uops",
        "Balancing uses uop-type information from all uops, not just the first two in an instruction",
        false};
    Param<bool> bal_clear_on_nonspec_flush{
        "bal_clear_on_nonspec_flush", "Clear partition balancing counters on non-spec flush", true};
    Param<std::vector<SchedulerType>> bal_sch_occupancy{
        "bal_sch_occupancy", "Tweak scheduler counters based on occupancy", std::vector<SchedulerType>{}};
    Param<unsigned> bal_occupancy_thresh{
        "bal_occupancy_thresh", "Tweak balancing counters if difference in scheduler occupancy is > N", 8};
    Param<bool> bal_round_robin{"bal_round_robin", "Round-robin tie-break of permutations", true};
    Param<bool> balancing_stats_collection{
        "balancing_stats_collection",
        "Collect stats on uop balancing across pipes and partitions (use with dump_at_end)",
        false};

    struct
    {
        Event swizzled{"counter_balance_swizzle", "Swizzled via counter balance"};
        EventData<unsigned> swizzle_index{"counter_balance_swizzle_index", "Index in permulation table that was used"};
    } event;

    const unsigned balancing_groups;
    const std::map<Uoppipe, unsigned> pipe_to_group;
    std::vector<SaturatingCounter> uop_differential;
    std::vector<std::array<int64_t, MAX_PARTITIONS>> balancing_stats;
    std::vector<int64_t> balancing_min;
    std::vector<int64_t> balancing_max;

    std::optional<unsigned> uop_balancing_group(unsigned index, const DecodedUopGroup& uop_group) const;

public:
    explicit IDRCounterBalancing(unsigned width, unsigned partitions = MAX_PARTITIONS);

    explicit operator bool() const { return counter_balance; }
    void balance(DecodedUopGroup& uop_group, const SchedulerCredits& sch);
    void update_stats(const DecodedUopGroup& uop_group);
    void flush(bool non_speculative);
    void bias_counter(Uoppipe pipe, int64_t delta)
    {
        if (auto group = pipe_to_group.find(pipe); group != pipe_to_group.end())
            uop_differential[group->second] += delta;
    }

    void dump(std::ostream&) const;
};
} // namespace ampere::core

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
