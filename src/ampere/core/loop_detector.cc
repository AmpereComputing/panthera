//
// Copyright (c) 2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <algorithm>

#include "ampere/core/loop_detector.h"
#include "ampere/archx/pmu.h"

namespace ampere::core {

void LoopDetector::start_collecting_loop(Fetch fetch, size_t restart_iterations = 0, size_t predicted_iterations = 0)
{
    auto& pred = fetch->get_prediction();
    mode = Mode::START_LOOP;
    loop_pc = pred.branch_pc;
    loop_target_pc = pred.target;
    instrs_this_collection = 0;
    predictions_this_collection = 0;
    if (predict_exit) {
        this->restart_iterations = restart_iterations;
        this->predicted_iterations = predicted_iterations;
    }
}

void LoopDetector::abort_loop(Fetch fetch)
{
    if (fetch->loop_start) {
        // If we were just starting the loop, cancel the loop_start signal
        // instead of starting and then aborting.
        fetch->loop_start = false;
        fetch->loop_collect = false;
    } else {
        fetch->loop_abort = true;
    }
    ev_loop_abort(fetch, ev_mode(static_cast<size_t>(mode)));
    mode = Mode::SEARCHING;
    saved_history.clear();
}

void LoopDetector::start_looping(Fetch fetch)
{
    fetch->loop_end = true;
    mode = Mode::LOOPING;
    ev_loop_enter(fetch);
    pmu_event(pmu_num_data(archx::PMU::LOOP_MODE_ENTER));
}

bool LoopDetector::is_call_return(const Prediction& prediction)
{
    return is_branch(prediction.branch_type) && (is_call(prediction.branch_type) || is_return(prediction.branch_type));
}

void LoopDetector::predict(Fetch fetch, BranchHistory& history, const BranchContext& context)
{
    auto& pred = fetch->get_prediction();

    // Always adjust the count of how many instructions we've seen since the
    // profiled PC. This count is reset by individual states as needed.
    if (!looping()) {
        instrs_this_collection += fetch->num_instructions();
        predictions_this_collection += 1;
    }

    if (enable_predictor && !looping() && !prediction_locked_out && (mode == Mode::SEARCHING || mode == Mode::LOCKING)
        && is_branch(pred.branch_type) && (!is_call(pred.branch_type) && !is_return(pred.branch_type))
        && (is_conditional(pred.branch_type) || !only_predict_conditionals)
        && (is_direct(pred.branch_type) || !only_predict_directs))
    {
        // lookup prediction table
        history.start_hashing(fetch->fetch_id);
        auto [set, history_hash] = history.hash(pred.branch_pc, predictor_history_comparison_depth, 2, 64);
        auto tag = PredictorTag(history_hash, pred.branch_pc, pred.target);
        auto hit = predictions.lookup(fetch, tag);

        if (hit && hit->confidence >= prediction_confidence_threshold) {
            ev_predict_collect(fetch, ev_pc(pred.branch_pc), ev_target(pred.target), ev_iterations(hit->iterations));
            pmu_event(pmu_num_data(archx::PMU::BPU_LOOP_PREDICTOR_HIT));

            // Aim to give the BPU enough lead time to catch up before the IDR
            // falls off the end of the last iteration. Assuming the BPU can
            // produce at least 4 instructions worth of predictions per cycle
            // and it will take 5 cycles from the time the IDR sends a signal
            // for the first fetch to make it through the BPU/ICF, tell the ICF
            // to restart 20 instructions worth of loop iterations early
            size_t stop_iterations = hit->iterations - 2;
            size_t iterations_early = (20 + hit->instrs_per_iteration - 1)
                                      / hit->instrs_per_iteration; // ceiling(20 / hit->instrs_per_iteration)
            size_t restart_iterations = 1;
            if (stop_iterations > iterations_early)
                restart_iterations = stop_iterations - iterations_early;
            start_collecting_loop(fetch, restart_iterations, stop_iterations);

            // increment the count of predictor table hits
            epoch_predictor_table_hits++;
            return;
        }
    }

    if (mode == Mode::SEARCHING) {
        if (is_branch(pred.branch_type) && !is_call_return(pred)
            && (!loop_branch_only_direct || is_direct(pred.branch_type)))
        {
            // We found a branch - watching it to see if it is part of a loop.
            mode = Mode::LOCKING;
            loop_pc = pred.branch_pc;
            instrs_this_collection = 0;
            predictions_this_collection = 0;
            saved_history = history.get_contents();
        }
    } else if (mode == Mode::LOCKING) {
        if (instrs_this_collection > iq_size || predictions_this_collection > pc_fifo_size) {
            // We have seen either 1) more instructions than would fit in our
            // IQ or 2) more predictions/cachelines than would fit in our PC
            // FIFO since the branch we're watching. Start searching for
            // another branch.
            mode = Mode::SEARCHING;
            saved_history.clear();
            pmu_event(pmu_num_data(archx::PMU::BPU_LOOP_ABORT_DURING_LOCKING));
        } else if (is_call_return(pred)) {
            // Our loops cannot contain calls or returns or we risk corrupting
            // the RSB
            mode = Mode::SEARCHING;
            pmu_event(pmu_num_data(archx::PMU::BPU_LOOP_ABORT_DURING_LOCKING));
            saved_history.clear();
        } else if (pred.branch_pc == loop_pc && instrs_this_collection >= unrolling_threshold) {
            if (history.same_contents(saved_history, history_comparison_depth + 1)) {
                // The branch history has saturated and we again encountered
                // the branch we are watching. Begin collecting the loop.
                // Note: We are using `history_comparison_depth + 1` here
                // because RTL does not consider the current branch into the
                // history when checking for saturation, so adding one makes us
                // equivalent.
                ev_detect_collect(fetch, ev_pc(pred.branch_pc), ev_target(pred.target));
                start_collecting_loop(fetch);
            } else if (wishy_washy_mode) {
                // We found the same branch, but the history did not match.
                // Search for another branch (maybe we latched onto a branch PC
                // seen multiple times in a short inner loop for which history
                // will never match, when we could find a matching history if
                // we find the branch for the outer loop instead)
                mode = Mode::SEARCHING;
                pmu_event(pmu_num_data(archx::PMU::BPU_LOOP_ABORT_DURING_LOCKING));
                saved_history.clear();
            } else {
                // When not in 'wishy washy' mode, we do not try to lock onto
                // any other branch PC if the one we already locked onto
                // recurs, so keep watching this one.
                instrs_this_collection = 0;
                predictions_this_collection = 0;
                saved_history = history.get_contents();
            }
        }
    } else if (mode == Mode::START_LOOP) {
        if (instrs_this_collection > iq_size || predictions_this_collection > pc_fifo_size || is_call_return(pred)) {
            // We seen one of 1) more instructions than would fit in our IQ, 2)
            // more predictions/cachelines than can fit in our PC FIFO, or 3) a
            // call/return since the last instance of the branch we were
            // watching. Start searching for another branch.
            mode = Mode::SEARCHING;
            pmu_event(pmu_num_data(archx::PMU::BPU_LOOP_ABORT_DURING_COLLECTING));
            saved_history.clear();
            return;
        }

        // Beginning to collect the loop. Signal to the ICF (will be IDR in
        // RTL) to stop removing fetches from the IQ.
        fetch->loop_start = true;
        fetch->loop_collect = true;
        fetch->loop_iterations = predicted_iterations;
        fetch->loop_restart_iterations = restart_iterations;

        if (pred.branch_pc == loop_pc && instrs_this_collection >= unrolling_threshold) {
            if (loop_target_pc != pred.target) {
                // We found the same PC, but the target is different, so abort
                // the loop
                pmu_event(pmu_num_data(archx::PMU::BPU_LOOP_ABORT_DURING_COLLECTING));
                abort_loop(fetch);
                return;
            }
            // A single fetch encompasses the entire loop, so signal that in
            // addition to the first fetch in the loop, this is also the last.
            // Begin looping immediately.
            start_looping(fetch);
        } else {
            mode = Mode::COLLECTING_LOOP;
        }
    } else if (mode == Mode::COLLECTING_LOOP) {
        if (instrs_this_collection > iq_size || predictions_this_collection > pc_fifo_size || is_call_return(pred)) {
            // We seen one of 1) more instructions than would fit in our IQ, 2)
            // more predictions/cachelines than can fit in our PC FIFO, or 3)
            // in our PC FIFO. We must have predicted the loop differently this
            // iteration since we overflowed our instruction counter. We need
            // to abort the loop.
            pmu_event(pmu_num_data(archx::PMU::BPU_LOOP_ABORT_DURING_COLLECTING));
            abort_loop(fetch);
            return;
        }

        fetch->loop_collect = true;
        if (pred.branch_pc == loop_pc && instrs_this_collection >= unrolling_threshold) {
            if (loop_target_pc != pred.target) {
                // We found the same PC, but the target is different, so abort
                // the loop
                pmu_event(pmu_num_data(archx::PMU::BPU_LOOP_ABORT_DURING_COLLECTING));
                abort_loop(fetch);
                return;
            }
            // We found the branch which ends the loop. Signal to the ICF (will
            // be IDR in RTL) to begin looping out of the IQ.
            start_looping(fetch);
        }
    }
}

/*
 * If there was a "loopable" loop detected, record this entry in the table for
 * this branch PC/history, increasing confidence of a matching entry if one
 * exists, or inserting a new entry if it doesn't. If this wasn't a "loopable"
 * loop, decrease confidence and/or invalidate any existing entry.
 */
void LoopDetector::predictor_end_loop(Txn transaction)
{
    // Consider a branch "loopable" if the branch history has saturated to a
    // certain depth (default 4 taken branches) and we have seen at least three
    // iterations of it (it takes us one iteration to enter loop mode, one to
    // come out, so any fewer than 3 means we don't get to stay in loop mode at
    // all).
    bool loopable =
        predictor_saturated && predictor_iterations > 3
        && (predictor_iterations * predictor_iteration_instr_count) >= minimum_predicted_looped_instructions;

    auto hit = predictions.lookup(transaction, *predictor_loop);
    if (hit) {
        if ((!predictor_match_iteration_count || hit->iterations == predictor_iterations) && loopable) {
            // increase the confidence of this entry if it is still considered
            // "loopable" and the iteration count matches
            if (hit->confidence < prediction_max_confidence) {
                hit->confidence++;
            }
            ev_increase_confidence(transaction,
                                   ev_pc(predictor_loop->branch_pc),
                                   ev_target(predictor_loop->loop_target),
                                   ev_iterations(predictor_iterations),
                                   ev_confidence(hit->confidence));
        } else {
            // If the loop iterations didn't match or it wasn't "loopable",
            // decrease this entry's confidence
            hit->confidence--;
            ev_decrease_confidence(transaction,
                                   ev_pc(predictor_loop->branch_pc),
                                   ev_target(predictor_loop->loop_target),
                                   ev_orig_iterations(hit->iterations),
                                   ev_iterations(predictor_iterations),
                                   ev_confidence(hit->confidence));

            // invalidate the entry if its confidence drops to 0
            if (hit->confidence == 0)
                predictions.invalidate(transaction, *predictor_loop);
        }
    } else if (loopable) {
        // If the loop missed the predictor table but was "loopable", fill it
        predictions.refill(
            transaction, *predictor_loop, PredictorEntry(predictor_iterations, predictor_iteration_instr_count));
    }

    ev_reset_tracking(transaction,
                      ev_pc(predictor_loop->branch_pc),
                      ev_target(predictor_loop->loop_target),
                      ev_iterations(predictor_iterations),
                      ev_saturated(predictor_saturated));
    predictor_loop.reset();
}

/*
 * Use information about taken branches to build the prediction table. Only
 * called for taken branches, `instrs_since_last_taken_branch` holds the number
 * of instructions seen since the last taken branch (including the current
 * branch).
 */
void LoopDetector::train_predictor(const BranchInfo& br_info,
                                   BranchHistory& history,
                                   uint64_t instrs_since_last_taken_branch)
{
    auto& fetch = br_info.fetch;
    assert(br_info.taken && is_branch(br_info.type));

    if (predictor_loop) {
        // If we are already watching a branch...
        if (instrs_since_last_taken_branch > iq_size) {
            // bail out if we have seen more instructions than fit within the
            // IQ (this may either mean that the loop was too long, or that we
            // just fell off the end of the loop)
            ev_bailing_size(fetch);
            predictor_end_loop(fetch);
        } else if (is_call(br_info.type) || is_return(br_info.type)
                   || (only_predict_conditionals && !is_conditional(br_info.type))
                   || (only_predict_directs && !is_direct(br_info.type)) || predictor_loop->branch_pc != br_info.pc
                   || predictor_loop->loop_target != br_info.target)
        {
            ev_bailing_off_loop(fetch);
            predictor_end_loop(fetch);
        } else {
            predictor_iteration_instr_count = instrs_since_last_taken_branch;
            predictor_iterations++;
            ev_prediction_iteration(fetch, ev_iterations(predictor_iterations));
            if (!predictor_saturated) {
                if (history.same_contents(predictor_last_history, predictor_history_saturation_depth)) {
                    predictor_saturated = true;
                } else {
                    predictor_last_history = history.get_contents();
                }
            }
        }
    }

    // Intentionally separate from above `if`, because predictor_loop can get
    // reset above and, if it does, we want to try to re-start watching
    if (!predictor_loop && (is_conditional(br_info.type) || !only_predict_conditionals)
        && (is_direct(br_info.type) || !only_predict_directs) && br_info.target < br_info.pc)
    {
        // If we see a backwards-facing branch when not already watching one,
        // record the initial branch history and branch PC; reset instruction
        // count to 0
        history.start_hashing(fetch->fetch_id);
        auto [set, tag] = history.hash(br_info.pc, predictor_history_comparison_depth, 2, 64);
        predictor_loop = PredictorTag(tag, br_info.pc, br_info.target);
        predictor_last_history = history.get_contents();
        predictor_saturated = false;
        predictor_iterations = 1;
        ev_predictor_start_watching(fetch, ev_pc(predictor_loop->branch_pc), ev_target(predictor_loop->loop_target));
    }
}

void LoopDetector::train(const BranchInfo& br_info, BranchHistory& history, const BranchContext& context)
{
    instrs_since_last_taken_branch += br_info.instrs_since_last_branch;
    if (br_info.taken) {
        train_predictor(br_info, history, instrs_since_last_taken_branch);
        instrs_since_last_taken_branch = 0;
    }
}

void LoopDetector::recover(const Flush& flush_info,
                           BranchInfo* br_info,
                           BranchHistory& history,
                           const BranchContext& context)
{
    saved_history.clear();
    mode = Mode::SEARCHING;
}

void LoopDetector::clear(Fetch fetch, bool late_ittage_clear)
{
    if (abort_on_btb_clear || late_ittage_clear) {
        // If the BPU sends a clear before the ICF has observed a fetch with
        // fetch->loop_start == true, the ICF cannot easily know that it should not
        // collect that fetch's instructions for loop mode. This should only happen
        // when the fetch cleared is also the fetch beginning loop-collection, so
        // clear loop_start here (Note: this works because LoopDetector::clear is
        // called after LoopDetector::predict for a given fetch).
        fetch->loop_start = false;
        // Set loop_abort on the fetch so the ICF knows to abort loop mode when
        // it sees this clear
        fetch->loop_abort = true;

        saved_history.clear();
        mode = Mode::SEARCHING;
    }
}

void LoopDetector::loop_restart(const LoopRestart& restart)
{
    assert(looping());
    mode = Mode::SEARCHING;
    ev_loop_restart(
        ev_pc(restart->branch_pc), ev_target(restart->target_pc), ev_iterations(restart->completed_iterations));
}

} // namespace ampere::core
