//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#ifndef AMPERE_CORE_LOOP_PREDICTOR_TABLE_H
#define AMPERE_CORE_LOOP_PREDICTOR_TABLE_H

#include "ampere/archx/addr.h"
#include "ampere/uarch_common/cache.h"

namespace ampere {
namespace core {

using namespace archx;
using namespace uarch_common;

struct PredictorTag
{
    PredictorTag(uint64_t starting_history, ProgramCounter branch_pc, ProgramCounter target)
        : starting_history(starting_history), branch_pc(branch_pc), loop_target(target)
    {
    }

    inline bool operator<(const PredictorTag& other) const
    {
        if (branch_pc != other.branch_pc)
            return branch_pc < other.branch_pc;
        if (loop_target != other.loop_target)
            return loop_target < other.loop_target;
        return starting_history < other.starting_history;
    }

    uint64_t starting_history;  // recorded history from we first saw this branch within this loop
    ProgramCounter branch_pc;   // PC of the branch instruction
    ProgramCounter loop_target; // target of the loop (TODO is this necessary to record if we know we're the only taken
                                // branch in the loop?)
};

AMPERE_EVENT_DATA_FUNC(PredictorTag x, predictor_tag, x.branch_pc.value());

struct PredictorEntry
{
    PredictorEntry(size_t iterations, size_t instrs_per_iteration)
        : iterations{iterations}, instrs_per_iteration{instrs_per_iteration}
    {
    }
    size_t iterations{0};           // number of consecutive iterations the loop's branch is taken
    size_t instrs_per_iteration{0}; // number of instructions per iteration
    size_t confidence{1};           // number of times in a row we've seen a loop matching this signature
};

class LoopPredictorTable : public ManagedCache<PredictorTag, PredictorEntry, PredictorTag>
{
public:
    explicit LoopPredictorTable(const sc_core::sc_module_name& name,
                                const PredictorEntry& perfect_value = PredictorEntry{0, 0})
        : ManagedCache<PredictorTag, PredictorEntry, PredictorTag>(name, perfect_value)
    {
    }

    using Entry = typename ManagedCache<PredictorTag, PredictorEntry, PredictorTag>::Entry;

    Index compute_set(PredictorTag tag) const override
    {
        if (this->set_bits == 0)
            return 0;
        auto index = tag.branch_pc.extract_range(this->line_bits + this->set_bits - 1, this->line_bits);
        if (index >= this->num_sets)
            throw std::logic_error("set index is larger than the number of sets");
        return index;
    }

    PredictorTag compute_tag(PredictorTag tag) const override { return tag; }
};

} // namespace core
} // namespace ampere

#endif // AMPERE_CORE_LOOP_PREDICTOR_TABLE_H

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
