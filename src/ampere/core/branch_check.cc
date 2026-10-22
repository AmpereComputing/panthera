//
// Copyright (c) 2019-2024 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/core/branch_check.h"

namespace ampere::core {

void update_fetch_prediction(Fetch& fetch, Instruction& instruction, SlotPrediction& slot)
{
    if (is_branch(slot.branch_type)) {
        auto target = slot.use_alternate ? slot.alternate.target : slot.primary.target;
        ProgramCounter predicted_next_pc = target.has_value() ? *target : NON_CANONICAL_TARGET;
        instruction->fetch->update_prediction(
            {slot.branch_type, instruction->archx_instruction->program_counter(), predicted_next_pc},
            true /*truncate if longer*/);
    } else {
        instruction->fetch->update_prediction(Prediction{}, true /*truncate if longer*/);
    }
}

/*
 * Check if the given instruction should cause a frontend clear based on new
 * information from the instruction encoding that was not known at
 * prediction-time.
 *
 * If we believe the prediction can be improved based on this new information,
 * update the prediction(s) in-place. If a clear is needed, return the
 * associated FlushType (a return value of std::nullopt indicates no clear is
 * needed).
 */
std::optional<FlushType> branch_check(Fetch& fetch, Instruction& instruction)
{
    auto arch_inst = instruction->archx_instruction;
    auto instruction_branch_type = arch_inst->branch_type();
    auto& slot_pred = fetch->line_prediction.at((arch_inst->program_counter() - fetch->cache_line()) / 4);
    const auto& prediction = fetch->get_prediction();
    // 'predicted_taken_branch' means prior to branch check, we are predicting
    // that the current instruction is a taken branch and is determining our
    // exit from the cacheline
    bool predicted_taken_branch = prediction.is_taken() && prediction.branch_pc == arch_inst->program_counter();
    // 'slot_taken' means that the slot prediction for this instruction was
    // taken. This can differ from `predicted_taken_branch` above on a BTB
    // miss, for example
    bool slot_taken = slot_pred.use_alternate ? slot_pred.alternate.taken : slot_pred.primary.taken;

    // The prediction BranchType should only be UNSPECIFIED if we are
    // short-circuiting the BPU with the perfect predictor.
    if (prediction.branch_type == BranchType::UNSPECIFIED) {
        return std::nullopt;
    }

    if (predicted_taken_branch) {
        // Avoid sending branch checks in some cases if we are on the wrong
        // path because these instructions' encodings will always appear to be
        // `add x0, x0, #1` even when the workload's 'real' wrong-path
        // instructions would not be. This confuses right-path prediction.
        bool artificial_enc = arch_inst->artificial_encoding();

        // BPU predicted a branch, but the fetched instruction isn't a branch
        if (!is_branch(instruction_branch_type) && !artificial_enc) {
            slot_pred.branch_type = BranchType::NONE;
            update_fetch_prediction(fetch, instruction, slot_pred);
            return FlushType::BRANCH_CHECK_WRONG_TYPE;
        }
        // BPU predicted a different branch type than the type of the fetched instruction
        else if (instruction_branch_type != slot_pred.branch_type && !artificial_enc)
        {
            slot_pred.branch_type = instruction_branch_type;

            // If we found a conditional branch, restore taken/not-taken from
            // saved information from the conditional predictors
            if (is_conditional(instruction_branch_type)) {
                slot_pred.primary.predictor = slot_pred.conditional_primary_predictor;
                slot_pred.alternate.predictor = slot_pred.conditional_alternate_predictor;
                slot_pred.primary.taken = slot_pred.conditional_primary_taken;
                slot_pred.alternate.taken = slot_pred.conditional_alternate_taken;
                slot_pred.use_alternate = slot_pred.conditional_use_alternate;
                // Also update the variable holding the slot direction
                // prediction for use later in this function
                slot_taken = slot_pred.use_alternate ? slot_pred.alternate.taken : slot_pred.primary.taken;
                if (!slot_taken)
                    slot_pred.branch_type = BranchType::NONE;
            } else {
                slot_pred.use_alternate = false;
                slot_pred.primary.taken = true;
            }

            // Determine the target of the branch from the instruction encoding
            // if direct or predict from saved RSB top of stack if a return.
            // Otherwise, fall back to non-canonical target as a predicted target and
            // wait for misprediction.
            if (is_direct(instruction_branch_type)) {
                auto& pred = slot_pred.use_alternate ? slot_pred.alternate : slot_pred.primary;
                if (is_unconditional(instruction_branch_type) || slot_taken) {
                    pred.target = arch_inst->branch_target();
                } else {
                    pred.target = arch_inst->program_counter() + 4;
                }
            } else {
                // Go to a non-canonical target here and stall waiting
                // for misprediction
                slot_pred.primary.target = NON_CANONICAL_TARGET;
            }

            update_fetch_prediction(fetch, instruction, slot_pred);
            return FlushType::BRANCH_CHECK_WRONG_TYPE;
        }
        // BPU predicted a direct branch, but the wrong target
        else if (is_direct(instruction_branch_type) && prediction.target != arch_inst->branch_target())
        {
            if (slot_pred.use_alternate)
                slot_pred.alternate.target = arch_inst->branch_target();
            else
                slot_pred.primary.target = arch_inst->branch_target();
            update_fetch_prediction(fetch, instruction, slot_pred);
            return FlushType::BRANCH_CHECK_TARGET;
        }
    } else {
        bool conditional_taken = slot_pred.conditional_use_alternate ? slot_pred.conditional_alternate_taken
                                                                     : slot_pred.conditional_primary_taken;
        // BPU did not predict a taken branch for this instruction, but we found an unconditional direct
        if (instruction_branch_type == BranchType::UNCONDITIONAL_DIRECT) {
            slot_pred.use_alternate = false;
            slot_pred.primary.taken = true;
            slot_pred.primary.target = arch_inst->branch_target();
            slot_pred.branch_type = BranchType::UNCONDITIONAL_DIRECT;
            update_fetch_prediction(fetch, instruction, slot_pred);
            return FlushType::BRANCH_CHECK_UNCOND_BRANCH;
            // BPU did not predict a taken branch for this instruction, but we found a direct call
        } else if (instruction_branch_type == BranchType::CALL_DIRECT) {
            slot_pred.use_alternate = false;
            slot_pred.primary.taken = true;
            slot_pred.primary.target = arch_inst->branch_target();
            slot_pred.branch_type = BranchType::CALL_DIRECT;
            update_fetch_prediction(fetch, instruction, slot_pred);
            return FlushType::BRANCH_CHECK_CALL_DIRECT;
            // BPU did not predict a taken branch for this instruction, but we found an unconditional indirect
        } else if (is_indirect(instruction_branch_type) && is_unconditional(instruction_branch_type)) {
            slot_pred.use_alternate = false;
            slot_pred.primary.taken = true;
            // We don't have enough information to reasonably and securely
            // predict an indirect target, so we force ourselves to a
            // non-canonical address
            slot_pred.primary.target = NON_CANONICAL_TARGET;
            slot_pred.branch_type = instruction_branch_type;
            update_fetch_prediction(fetch, instruction, slot_pred);
            if (instruction_branch_type == BranchType::RETURN) {
                return FlushType::BRANCH_CHECK_RETURN;
            } else if (instruction_branch_type == BranchType::CALL_INDIRECT) {
                return FlushType::BRANCH_CHECK_CALL_INDIRECT;
            } else if (instruction_branch_type == BranchType::UNCONDITIONAL_INDIRECT) {
                return FlushType::BRANCH_CHECK_UNCOND_INDIRECT;
            } else {
                AMPERE_FATAL("Unhandled branch type {} for branch check", instruction_branch_type);
            }
            // BPU predicted this 'slot' as taken, but did not find a target in the
            // BTB. Because we can calculate the target (it's direct), clear rather
            // than wait for a misprediction.
        } else if (instruction_branch_type == BranchType::CONDITIONAL_DIRECT && conditional_taken) {
            // Restore the direction prediction information from the original
            // conditional predictors
            slot_pred.primary.predictor = slot_pred.conditional_primary_predictor;
            slot_pred.alternate.predictor = slot_pred.conditional_alternate_predictor;
            slot_pred.primary.taken = slot_pred.conditional_primary_taken;
            slot_pred.alternate.taken = slot_pred.conditional_alternate_taken;
            slot_pred.use_alternate = slot_pred.conditional_use_alternate;
            slot_taken = true;
            auto& pred = slot_pred.use_alternate ? slot_pred.alternate : slot_pred.primary;
            pred.target = arch_inst->branch_target();

            slot_pred.branch_type = BranchType::CONDITIONAL_DIRECT;
            update_fetch_prediction(fetch, instruction, slot_pred);

            return FlushType::BRANCH_CHECK_COND_BRANCH;
        }

        if (slot_pred.branch_type == BranchType::NONE && slot_taken) {
            // Strange case, but if this is one of those slots that the BTB was unaware of and yet we still got
            // "taken" set (due to aliasing or whatever), then we actually need to *reset* the taken bit here so
            // that we don't get confused later on and think that "mispredicted_direction" implies not taken.
            //
            // You can think of it as "resolving" the notion of taken at this point.
            //
            if (slot_pred.use_alternate)
                slot_pred.alternate.taken = false;
            else
                slot_pred.primary.taken = false;
        }
    }
    return std::nullopt;
}

} // namespace ampere::core
