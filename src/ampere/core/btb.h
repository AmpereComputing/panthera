//
// Copyright (c) 2019-2020, 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include <nlohmann/json.hpp>

#include "ampere/archx/addr.h"
#include "ampere/archx/el.h"
#include "ampere/archx/instruction.h"
#include "ampere/core/prediction.h"
#include "ampere/core/predictor.h"
#include "ampere/uarch_common/crp/cache_replacement_policy.h"
#include "ampere/uarch_common/crp/lru.h"
#include "ampere/util/bits.h"

namespace ampere::core {

using json = nlohmann::json;

using archx::BranchType;
using archx::ProgramCounter;
using namespace ampere::uarch_common;
using namespace ampere::sc;
using util::bitsize;

struct BTBEntry
{
    BTBEntry() : tag{0}, el{EL0}, offset{0}, branch_type{BranchType::NONE}, target{0}, branch_pc{0} {}

    BTBEntry(unsigned tag,
             archx::EL el,
             unsigned offset,
             BranchType branch_type,
             ProgramCounter target,
             bool target_nextline,
             ProgramCounter branch_pc)
        : tag{tag},
          el{el},
          offset{offset},
          branch_type{branch_type},
          target{target},
          target_nextline{target_nextline},
          branch_pc{branch_pc}
    {
    }

    BTBEntry(const BTBEntry&) = default;

    unsigned tag;
    archx::EL el;
    unsigned offset;

    BranchType branch_type;
    ProgramCounter target;
    bool target_nextline;     // set to true iff we can guarantee the remainder of
                              // the cacheline at `target` contains no branches
    ProgramCounter branch_pc; // needed to calculate branch warmup since RTL
                              // stores target relative to branch PC

    bool is_valid{false};
};

void to_json(json& j, const BTBEntry& entry);
void from_json(const json& j, BTBEntry& entry);

class BTB : public Module, public Predictor
{
public:
    explicit BTB(const sc_core::sc_module_name& name, size_t bank_bits);
    explicit BTB(const sc_core::sc_module_name& name);

    Param<size_t> sets{"sets", "number of sets in the BTB", 2048};
    Param<size_t> ways{"ways", "number of ways in the BTB", 4};
    Param<size_t> tag_bits{"tag_bits", "tag bits used in the BTB", 12};
    Param<bool> el_tagged{"el_tagged", "BTB entries are tagged by EL in addition to normal tag", false};

    Param<std::uint64_t> target_bits{"target_bits", "max bits of target we can store in the BTB", 26};

    Submodule<CacheReplacementPolicy, LRU> crp{"cache_replacement_policy"};

    Param<bool> use_half_cache_line_bit_in_index{
        "use_half_cache_line_bit_in_index", "use bit 5 of the cache line as part of the index", false};
    Param<bool> selective_btc_clear_invalidation{
        "selective_btc_clear_invalidation",
        "Invalidate only the way used to make the original prediction on BTC clears",
        false};
    Param<bool> paired_btc_clear_invalidation{
        "paired_btc_clear_invalidation",
        "Invalidate the way used to make the prediction on BTC clears, and its pair",
        false};

    void predict(Fetch fetch, BranchHistory& history, const BranchContext& context) override
    {
        do_predict(fetch, history, context, true);
    }

    void predict_inst(Fetch fetch, BranchHistory& history, const BranchContext& context) override
    {
        do_predict(fetch, history, context, false);
    }

    void train(const BranchInfo& br_info, BranchHistory& history, const BranchContext& context) override;

    void update_lru(const BranchInfo& br_info, const BranchContext& context);

    void invalidate_line(Fetch fetch, const BranchContext& context);

    bool can_provide_target(BranchType branch_type, ProgramCounter branch_pc, ProgramCounter target_pc) const;

    void save_state(json& j);
    void restore_state(const json& j);

protected:
    Event read_event{"read", "the BTB was looked up"};
    Event match_event{"match", "an entry from the BTB was matched by a lookup"};
    Event update_event{"update", "BTB update"};
    Event update_lru_event{"update_lru", "BTB update LRU only"};
    EventData<ProgramCounter> br_pc{"br_pc", "PC of branch instruction"};
    EventData<ProgramCounter> br_target{"br_target", "Branch target"};
    EventData<bool> br_target_nextline{"br_target_nextline", "Prediction at branch target is nextline"};
    EventData<int> br_type{"br_type", "Branch type"};
    EventData<int> br_offset{"offset", "offset (instruction number) in a cache line"};
    EventData<bool> hit_data{"hit", "fetch hit/missed the BTB"};
    EventData<unsigned> btb_set{"set", "BTB set"};
    EventData<unsigned> btb_tag{"tag", "BTB tag"};

    using SetType = std::vector<BTBEntry>;
    std::vector<SetType> array;

    const unsigned line_bits = bitsize(63);

    // Note: index_bits is the sum of both the number of bits used to index into
    // banks, and indices to the sets within each bank
    unsigned index_bits;
    unsigned bank_bits{0};
    unsigned set_bits;

    std::uint64_t age{0};

    std::tuple<unsigned, unsigned> compute_index(ProgramCounter address, BPUBank bank) const;

    void do_predict(Fetch fetch, BranchHistory& history, const BranchContext& context, bool post_event);
    std::pair<int, bool> pick_way(unsigned set_index, unsigned tag, archx::EL el, unsigned offset);
    int pick_matching_way(unsigned set_index, unsigned tag, archx::EL el, unsigned offset);
    void invalidate_all_ways(Fetch fetch);
    void invalidate_paired_ways(Fetch fetch, const BranchContext& context);
    void invalidate_one_way(Fetch fetch, const BranchContext& context);
};

} // namespace ampere::core
