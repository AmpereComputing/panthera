//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include "ampere/sc/sc.h"
#include "ampere/core/l2/crp/l2_replacement_policy.h"

namespace ampere::core {

using namespace ampere::sc;

class L2RRIP : public L2ReplacementPolicy, poly<L2RRIP>
{
public:
    explicit L2RRIP(const sc_core::sc_module_name& name) : L2ReplacementPolicy(name) {}

    // Main RRIP parameters
    Param<int> rrip_max{"rrip_max", "Maximum RRIP age value (usually 2^n-1)", 3};
    Param<int> insert_delta{"insert_delta", "When not filling with age rrip_max, fill with age (rrip_max - N)", 1};
    Param<bool> prefetch_fills_act_like_demand{
        "prefetch_fills_act_like_demand", "L2 prefetcher fills affect replacement like demand fills", false};
    Param<bool> ignore_in_l1i{"ignore_in_l1i", "Do not avoid evicting lines which may be in the IL1", true};
    Param<bool> ignore_in_l1d{"ignore_in_l1d", "Do not avoid evicting lines which may be in the DL1", true};
    Param<bool> prefetch_priority{
        "prefetch_priority", "If ages are equal, prefer a victim with its prefetched bit set", false};
    Param<bool> l1_priority{"l1_priority", "If ages are equal, prefer a victim not in an L1", true};
    Param<bool> write_hit_updates_age{"write_hit_updates_age", "Updates (write hits) will set age to 0", true};
    Param<bool> pf_hit_like_fill{"pf_hit_like_fill", "Hit on prefetched line updates age like a fill", true};
    Param<bool> dirty_tiebreak{"dirty_tiebreak", "If ages are equal, prefer a victim which is modified", false};
    Param<bool> use_round_robin{"use_round_robin", "Victim select breaks ties using a bank-wide round-robin", true};

    void init(uint64_t banks, uint64_t sets, uint64_t ways, uint64_t ways_mask) override;
    void invalidate(uint64_t bank, uint64_t set, uint64_t way) override;
    void touch(uint64_t bank,
               uint64_t set,
               uint64_t way,
               TouchOp op,
               bool demand,
               const Set& set_contents,
               MemReq req = nullptr) override;
    uint64_t victim(uint64_t bank, uint64_t set, const Set& set_contents, uint64_t way_mask) override;

protected:
    void set_age(uint64_t banks, uint64_t sets, uint64_t ways, TouchOp op, int new_age);
    uint64_t inc_mod_ways(uint64_t x);

    uint64_t sets{0};
    uint64_t ways{0};
    uint64_t banks{0};
    uint64_t ways_mask{0xff};

    std::vector<uint64_t> start_ptr;
    std::vector<std::vector<std::vector<int>>> age;

    // Instrumentation from here down -- not modeling real hardware

    struct LineStats
    {
        bool valid{false};
        bool hit_since_filled{false};
        bool reuse_prediction{false}; // true == predict line will be reused
        MemOp::Requester origin{MemOp::Requester::NONE};
    };

    void touch_stats(uint64_t bank, uint64_t set, uint64_t way, TouchOp op, bool demand, MemReq req);

    std::vector<std::vector<std::vector<LineStats>>> stats;

    Event ev_crp_code_no_reuse_correct{"crp_code_no_reuse_correct", "Code line predicted not to be reused, correctly"};
    Event ev_crp_code_no_reuse_mispred{"crp_code_no_reuse_mispred",
                                       "Code line predicted not to be reused, incorrectly"};
    Event ev_crp_code_reuse_correct{"crp_code_reuse_correct", "Code line predicted to be reused, correctly"};
    Event ev_crp_code_reuse_mispred{"crp_code_reuse_mispred", "Code line predicted to be reused, incorrectly"};
    Event ev_crp_data_no_reuse_correct{"crp_data_no_reuse_correct", "Data line predicted not to be reused, correctly"};
    Event ev_crp_data_no_reuse_mispred{"crp_data_no_reuse_mispred",
                                       "Data line predicted not to be reused, incorrectly"};
    Event ev_crp_data_reuse_correct{"crp_data_reuse_correct", "Data line predicted to be reused, correctly"};
    Event ev_crp_data_reuse_mispred{"crp_data_reuse_mispred", "Data line predicted to be reused, incorrectly"};
    Event ev_crp_mmu_no_reuse_correct{"crp_mmu_no_reuse_correct",
                                      "Page table line predicted not to be reused, correctly"};
    Event ev_crp_mmu_no_reuse_mispred{"crp_mmu_no_reuse_mispred",
                                      "Page table line predicted not to be reused, incorrectly"};
    Event ev_crp_mmu_reuse_correct{"crp_mmu_reuse_correct", "Page table line predicted to be reused, correctly"};
    Event ev_crp_mmu_reuse_mispred{"crp_mmu_reuse_mispred", "Page table line predicted to be reused, incorrectly"};

    Event ev_crp_demand_fill{"crp_demand_fill", "crp_demand_fill"};
    Event ev_crp_prefetch_fill{"crp_prefetch_fill", "crp_prefetch_fill"};
    Event ev_crp_demand_hit{"crp_demand_hit", "crp_demand_hit"};
    Event ev_crp_prefetch_hit{"crp_prefetch_hit", "crp_prefetch_hit"};

    Event ev_crp_evict{"crp_evict", "crp_evict"};
};

} // namespace ampere::core
