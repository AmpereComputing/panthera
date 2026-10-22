//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#ifndef AMPERE_CORE_RETURN_STACK_H
#define AMPERE_CORE_RETURN_STACK_H

#include <map>

#include "ampere/archx/addr.h"
#include "ampere/sc/sc.h"
#include "ampere/core/fetch_id.h"
#include "ampere/core/predictor.h"
#include "fetch.h"

namespace ampere::core {

using archx::ProgramCounter;
using namespace sc;

class ReturnStack : public Module, public Predictor
{
private:
    struct StackEntry
    {
        ProgramCounter pc;
        archx::EL el;
        FetchId previous;
        bool call_committed{false};
    };

    uint32_t committed_call_num{0};

    std::map<FetchId, StackEntry> calls;
    std::map<FetchId, FetchId> returns;

    FetchId tos{0};

    Event spec_call_event{"spec_call", "speculative call (Branch with Link) allocates in the RSB"};
    Event spec_ret_event{"spec_ret", "speculative return in the RSB"};
    Event commit_call_event{"commit_call", "call (Branch with Link) commits in the RSB"};
    Event commit_ret_event{"commit_ret", "return commits (and deallocates) in the RSB"};
    EventData<FetchId> call_fetch_id_data{"call_fetch_id", "fetch id of matching call"};

public:
    Param<std::uint32_t> max_size{"max_size", "maximum number of entries in RSB", 64};
    Param<std::uint32_t> committed_call_threshold{
        "committed_call_threshold",
        "number of committed calls needed in a full RSB before replacing RSB entries on a speculative call",
        32};
    Param<bool> ignore_untracked_commits{
        "ignore_untracked_commits",
        "Ignore commits that are not tracked by RSB - eg in perfect_el0 mode, EL0 calls and rets bypass the RSB",
        false};

    explicit ReturnStack(const sc_core::sc_module_name& name) : Module{name} {}

    void predict(Fetch fetch, BranchHistory& history, const BranchContext& context) override;
    void train(const BranchInfo& br_info, BranchHistory& history, const BranchContext& context) override;

    void spec_call(Fetch fetch, ProgramCounter pc, const BranchContext& context);
    std::optional<ProgramCounter> spec_ret(Fetch fetch, const BranchContext& context);

    void commit_call(Fetch fetch);
    std::optional<ProgramCounter> commit_ret(Fetch fetch);

    void flush(FetchId fetch_id);

    void clear();

    bool is_full() const;
};

} // namespace ampere::core

#endif // AMPERE_CORE_RETURN_STACK_H
