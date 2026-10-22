//
// Copyright (c) 2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#ifndef AMPERE_CORE_LOOP_DETECTOR_H
#define AMPERE_CORE_LOOP_DETECTOR_H

#include "ampere/core/branch_history.h"
#include "ampere/core/fetch.h"
#include "ampere/core/predictor.h"
#include "ampere/core/loop_predictor_table.h"
#include "ampere/core/loop_restart.h"
#include "ampere/sc/addr.h"

namespace ampere::core {

using namespace archx;
using namespace sc;

class LoopDetector : public Module, public Predictor
{
public:
    LoopDetector(const sc_core::sc_module_name& name) : Module{name} {}

    void predict(Fetch fetch, BranchHistory& history, const BranchContext& context) override;
    void train(const BranchInfo& br_info, BranchHistory& history, const BranchContext& context) override;
    void recover(const Flush& flush_info,
                 BranchInfo* br_info,
                 BranchHistory& history,
                 const BranchContext& context) override;
    void loop_restart(const LoopRestart& restart);
    void clear(Fetch fetch, bool late_ittage_clear);
    size_t lockout_epoch_prediction_hits() const { return epoch_predictor_table_hits; };
    void end_lockout_epoch() { epoch_predictor_table_hits = 0; }
    void begin_prediction_lockout() { prediction_locked_out = true; }
    void end_prediction_lockout() { prediction_locked_out = false; }

    enum class Mode {
        SEARCHING = 1,  // Searching for a branch to watch
        LOCKING = 2,    // Gaining confidence the branch we are watching is a loop
        START_LOOP = 3, // We believe we are ready to loop, next fetch will be first 'collected' fetch (can only be in
                        // this state for a single fetch)
        COLLECTING_LOOP = 4, // We are collecting a loop into the IDR
        LOOPING = 5,         // BPU is off, IDR is fetching out of IQ
    };

    Mode get_mode() const { return mode; }
    bool looping() const { return mode == Mode::LOOPING; }

private:
    Event ev_detect_collect{
        "detect_collect",
        "Starting to collect a loop upon detecting the same history between two instances of this branch PC"};
    Event ev_predict_collect{"predict_collect", "Starting to collect a loop upon hitting in the loop prediction table"};
    Event ev_loop_abort{"loop_abort", "Abort an in-progress loop collection"};
    Event ev_loop_enter{"loop_enter", "BPU is entering loop mode"};
    Event ev_loop_restart{"loop_restart", "Restart after a loop is complete"};

    Event ev_reset_tracking{"predictor_reset_tracking", "Reset the loop predictor's tracking"};
    Event ev_increase_confidence{"increase_confidence",
                                 "increase the confidence of an existing prediction table entry"};
    Event ev_decrease_confidence{"decrease_confidence",
                                 "decrease the confidence of an existing prediction table entry"};
    Event ev_prediction_iteration{"prediction_iteration", "prediction iteration count update"};
    Event ev_predictor_start_watching{"predictor_start_watching",
                                      "predictor is starting to watch a potential branch PC"};
    Event ev_bailing_size{"bailing_size", "predictor bailed on watching loop because it was too large"};
    Event ev_bailing_off_loop{"bailing_off_loop", "predictor bailed because it found a PC not in the loop"};
    EventData<ProgramCounter> ev_pc{"program_counter", "Program Counter"};
    EventData<ProgramCounter> ev_target{"branch_target", "Program Counter"};
    EventData<size_t> ev_mode{"mode", "Loop detector mode"};
    EventData<bool> ev_saturated{"saturated", "whether the loop predictor is saturated"};
    EventData<size_t> ev_iterations{"iterations", "number of iterations"};
    EventData<size_t> ev_confidence{"confidence", "confidence"};
    EventData<size_t> ev_orig_iterations{"orig_iterations", "number of iterations"};

    Event pmu_event{"pmu", "A PMU event has occurred"};
    EventData<PMU> pmu_num_data{"event", "PMU event number"};

    // This should be set to match the core-level parameter 'icf.instruction_fifo.size'
    Param<size_t> iq_size{"iq_size", "Size of IQ to emulate", 32};
    // The PC FIFO is not modeled in panthera, so fake the limitation here
    Param<size_t> pc_fifo_size{"pc_fifo_size", "Size of PC FIFO to emulate", 8};
    Param<size_t> unrolling_threshold{
        "unrolling_threshold", "Unroll a loop until the unrolled loop contains at least this many instructions", 8};
    Param<size_t> history_comparison_depth{
        "history_comparison_depth", "Depth of branch history which must match to *detect* looping (0 means all)", 32};
    Param<bool> wishy_washy_mode{
        "wishy_washy_mode",
        "Save and compare branch history of different branch PCs as we search instead of staying locked onto one",
        true};
    Param<bool> abort_on_btb_clear{
        "abort_on_btb_clear", "Abort loop prediction and detection if we observe a BTB clear", true};
    Param<bool> enable_predictor{"enable_predictor", "Enable the loop predictor", false};
    Param<bool> predict_exit{"predict_exit", "Predict the exit of loops", false};
    Param<bool> only_predict_conditionals{"only_predict_conditionals", "Only predict conditional branches", true};
    Param<bool> only_predict_directs{"only_predict_directs", "Only predict direct branches", true};
    Param<bool> loop_branch_only_direct{
        "loop_branch_only_direct", "The loop branch of a detected loop must be a direct branch", true};
    Param<bool> predictor_match_iteration_count{
        "predictor_match_iteration_count",
        "Require a loop to have the same iteration count to increase confidence of prediction table entry ",
        true};
    Param<size_t> predictor_history_comparison_depth{
        "predictor_history_comparison_depth",
        "Depth of branch history which must match for a loop entry to increase the confidence of a previous entry in "
        "the prediction table, or to match it for prediction (0 means all)",
        32};
    Param<size_t> predictor_history_saturation_depth{"predictor_history_saturation_depth",
                                                     "Depth of branch history which must have saturated for a loop to "
                                                     "be inserted in the prediction table (0 means all)",
                                                     4};
    Param<size_t> prediction_confidence_threshold{
        "prediction_confidence_threshold",
        "The number of times the same loop must be seen in a row with the same starting branch history (to depth "
        "predictor_history_comparison_depth), the same number of iterations, and the same target to predict it",
        4};
    Param<size_t> prediction_max_confidence{"prediction_max_confidence", "The maximum confidence value", 8};
    Param<size_t> minimum_predicted_looped_instructions{
        "minimum_predicted_looped_instructions",
        "The minimum number of predicted looped instructions required to predict a loop",
        0};

    // helpers for changing loop detector state machine and setting flags on
    // Fetch objects
    void start_collecting_loop(Fetch fetch, size_t restart_iterations, size_t predicted_iterations);
    void abort_loop(Fetch fetch);
    void start_looping(Fetch fetch);

    bool is_call_return(const Prediction& prediction);
    size_t required_pc_fifo_entries();

    // helpers to build prediction table
    void predictor_end_loop(Txn transaction);
    void train_predictor(const BranchInfo& br_info, BranchHistory& history, uint64_t instrs_since_last_taken_branch);

    // Loop *detection* parameters
    ProgramCounter loop_pc{0x0_pc};
    ProgramCounter loop_target_pc{0x0_pc};
    size_t predicted_iterations{0}; // number of predicted iterations in the loop (only set if looping from predictor)
    size_t restart_iterations{0};   // number of looped iterations to wait before restarting BPU
    size_t instrs_this_collection{0};
    size_t predictions_this_collection{0};
    BranchHistoryQueue saved_history;
    Mode mode{Mode::SEARCHING};
    size_t epoch_predictor_table_hits{0}; // Count number of predictor table hits until cleared
    bool prediction_locked_out{false};

    // Loop *prediction* parameters - those used to update the prediction table
    // at commit-/train-time
    uint64_t instrs_since_last_taken_branch{0};
    std::optional<PredictorTag> predictor_loop;
    size_t predictor_iterations{0};
    size_t predictor_iteration_instr_count{0};
    BranchHistoryQueue predictor_last_history;
    bool predictor_saturated{false};

    // Predictor global state (saves observed loops while we're attempting to
    // figure out if we can predict them)
    LoopPredictorTable predictions{"prediction_table"};
};

} // namespace ampere::core

#endif // AMPERE_CORE_LOOP_DETECTOR_H
