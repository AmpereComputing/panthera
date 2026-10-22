//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "bp_rsb.h"

namespace ampere::core {

void ReturnStack::predict(Fetch fetch, BranchHistory& history, const BranchContext& context)
{
    if (is_return(fetch->get_prediction().branch_type)) {
        auto return_target = spec_ret(fetch, context);
        if (return_target.has_value()) {
            fetch->update_prediction_target(*return_target);

            auto slot = (fetch->get_prediction().branch_pc - fetch->cache_line()) / 4;
            auto& p = fetch->line_prediction[slot];

            p.alternate = p.primary;
            p.primary.predictor = this;
            p.primary.strength = BranchStrength::VERYSTRONG;
            p.primary.taken = true;
            p.primary.target = return_target;
            p.use_alternate = false;
            p.rsb_hit = true;
        }
    } else if (is_call(fetch->get_prediction().branch_type)) {
        spec_call(fetch, fetch->get_prediction().branch_pc, context);
    }
}

void ReturnStack::train(const BranchInfo& br_info, BranchHistory& history, const BranchContext& context)
{
    if (is_call(br_info.type))
        commit_call(br_info.fetch);
    else if (is_return(br_info.type))
        commit_ret(br_info.fetch);
}

void ReturnStack::spec_call(Fetch fetch, ProgramCounter pc, const BranchContext& context)
{
    // If all entries are occupied and number of committed calls is more than
    // the threshold, replace the oldest committed call with the new speculative call.
    if ((calls.size() == max_size) && (committed_call_num > committed_call_threshold)) {
        calls.erase(calls.begin());
        committed_call_num--;
    } else if (is_full())
        AMPERE_ERROR("{}: unexpected speculative call when RSB is full at fetch_id: {}, pc: 0x{}",
                     name(),
                     fetch->fetch_id,
                     fetch->program_counter);

    calls[fetch->fetch_id] = {pc, context.el, tos};
    tos = fetch->fetch_id;
    spec_call_event(fetch, pc);
}

std::optional<ProgramCounter> ReturnStack::spec_ret(Fetch fetch, const BranchContext& context)
{
    auto most_recent_call = calls.find(tos);

    if (most_recent_call == calls.end())
        return {};

    auto& entry = most_recent_call->second;

    auto ret_target = entry.pc + 4;

    // If the current EL does not match the TOS entry's EL, we do not use the TOS entry, but
    // update the TOS pointer. Since we are doing this speculatively, this entry *may* be used
    // later after a TOS restoration.
    if (entry.el != context.el) {
        ret_target = {};
    } else {
        spec_ret_event(fetch, ret_target, call_fetch_id_data(tos));
        returns[fetch->fetch_id] = tos;
    }

    tos = entry.previous;

    return ret_target;
}

void ReturnStack::commit_call(Fetch fetch)
{
    if (calls.find(fetch->fetch_id) == calls.end()) {
        if (ignore_untracked_commits)
            return;
        else
            AMPERE_ERROR(
                "{}: commit of non-tracked fetch id {}, pc 0x{}", name(), fetch->fetch_id, fetch->program_counter);
    }

    calls.find(fetch->fetch_id)->second.call_committed = true;
    committed_call_num++;
    commit_call_event(fetch);
}

std::optional<ProgramCounter> ReturnStack::commit_ret(Fetch fetch)
{
    auto this_return = returns.find(fetch->fetch_id);

    if (this_return == returns.end())
        return {};

    auto matching_call_id = this_return->second;
    auto matching_call = calls.find(matching_call_id);

    returns.erase(fetch->fetch_id);

    if (matching_call == calls.end())
        return {};

    auto& entry = matching_call->second;

    auto ret_target = entry.pc + 4;
    commit_ret_event(fetch, ret_target, call_fetch_id_data(matching_call_id));

    calls.erase(matching_call_id);
    AMPERE_ASSERT(committed_call_num != 0, "RSB committed call tracker (committed_call_num) underflow");
    committed_call_num--;

    return ret_target;
}

void ReturnStack::flush(FetchId fetch_id)
{
    auto flush_point_calls = calls.lower_bound(fetch_id);
    auto flush_point_returns = returns.lower_bound(fetch_id);

    bool restore_tos_from_calls = flush_point_calls != calls.end();
    bool restore_tos_from_returns = flush_point_returns != returns.end();

    if (restore_tos_from_calls && restore_tos_from_returns) {
        if (flush_point_calls->first < flush_point_returns->first)
            restore_tos_from_returns = false;
        else
            restore_tos_from_calls = false;
    }

    if (restore_tos_from_calls)
        tos = flush_point_calls->second.previous;
    else if (restore_tos_from_returns)
        tos = flush_point_returns->second;
    else
        return;

    calls.erase(flush_point_calls, calls.end());
    returns.erase(flush_point_returns, returns.end());

    committed_call_num = 0;
    for (auto iter : calls) {
        if (iter.second.call_committed)
            committed_call_num++;
    }
}

void ReturnStack::clear()
{
    calls.clear();
    returns.clear();
    tos = 0;
    committed_call_num = 0;
}

bool ReturnStack::is_full() const
{
    // RSB is full when all RSB entries are occupied and number of committed
    // entries is less than or equal to the threshold value.
    return (calls.size() == max_size) && (committed_call_num <= committed_call_threshold);
}
} // namespace ampere::core
