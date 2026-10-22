//
// Copyright (c) 2018-2024 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include "ampere/archx/driver.h"
#include "ampere/core/bob_entry.h"
#include "ampere/core/bp_tage.h"
#include "ampere/core/branch_history.h"
#include "ampere/core/fetch.h"
#include "ampere/core/flush.h"
#include "ampere/core/loop_detector.h"
#include "ampere/core/loop_restart.h"
#include "ampere/core/nnp.h"
#include "ampere/sc/sc.h"
#include "ampere/uarch_common/cache.h"

#define BPU_DEBUG(fmt, ...) \
    AMPERE_UNCATEGORIEZED_DEBUG("[{}] {:8} " fmt, sc_core::sc_time_stamp(), basename(), __VA_ARGS__)

namespace ampere::core {

using namespace ampere::sc;
using namespace ampere::uarch_common;

struct uRSBEntry
{
    ProgramCounter target;
    bool valid{false};
};

class BPU : public Module
{
public:
    explicit BPU(const sc_core::sc_module_name& name);

    Param<int> commits_per_cycle{"commits_per_cycle", "number of branch (uop) commits processed per cycle", 1};
    Param<bool> allow_merging_all_updates{
        "allow_merging_all_updates", "Allow merging all updates, regardless of reality", false};
    Param<bool> perfect{"perfect", "Predict branches perfectly", false};
    Param<bool> perfect_el0{"perfect_el0", "Predict perfectly and do not train for EL0 code", false};
    Param<bool> perfect_nnp{
        "perfect_nnp",
        "Magically do BTB/TAGE/ITTAGE predictions at the front of the pipe, making it look like NNP is perfect",
        false};
    Param<std::string> restore_warmup_file{"warmup_file", "Path at which to restore BPU warmup JSON file", ""};
    Param<bool> enable_pipeline_mispredict_event{"enable_pipeline_mispredict_event",
                                                 "Track if a branch would have been predicted correctly if not for"
                                                 " the delay between prediction and training",
                                                 false};
    Param<bool> update_stalls_predict{"update_stalls_predict", "Updates stall the prediction pipeline", true};
    Param<bool> ursb_enabled{"ursb_enabled", "Enable the micro-RSB", true};
    Param<bool> always_update{
        "always_update", "Always do a (1 cycle) update, irrespective of mispredict, BTB hit, etc", false};
    Param<bool> post_branch_history{
        "post_branch_history", "Post a branch history (string) with the br_train event", false};
    Param<size_t> feq_sleep_threshold{
        "feq_sleep_threshold", "Enable BPU to sleep when FEQ has fewer than this many free entries (0 disables)", 0};
    Param<size_t> feq_wake_threshold{
        "feq_wake_threshold", "Number of available FEQ entries required to wake up the BPU", 1};
    Param<bool> ld_enabled{"ld_enabled", "Enable the loop detector", false};
    Param<bool> update_history_while_looping{"update_history_while_looping", "Update history while looping", false};
    Param<CycleDelta> loop_prediction_lockout_epoch{
        "loop_prediction_lockout_epoch", "Number of cycles within which to count prediction hits", 512};
    Param<size_t> loop_prediction_lockout_hit_threshold{
        "loop_prediction_lockout_hit_threshold",
        "Threshold number of hits within the epoch to trigger lockout (0 disables)",
        16};
    Param<CycleDelta> loop_prediction_lockout_length{
        "loop_prediction_lockout_length",
        "Number of cycles to stop predicting loop entry for if lockout triggered",
        2048};
    Param<int> ittage_delay{"ittage_delay", "ITTAGE pipeline delay (cycles)", 0};
    Param<int> ittage_rmw_delay{"ittage_rmw_delay", "Cycles to do a read/modify/write update of the ITTAGE", 3};
    Param<int> ittage_write_delay{"ittage_write_delay", "Cycles to do a write-only update of the ITTAGE", 1};
    Param<bool> stall_on_any_indirect{
        "stall_on_any_indirect", "Stall the ICF one cycle if an indirect is detected anywhere in a line", false};
    Param<bool> np_on_flush{"np_on_flush",
                            "Use and train NP instead of NNP for first prediction following any GPC flush or BTC clear",
                            false};
    Param<bool> use_context_token{
        "use_context_token", "XOR a context token into tags of indirect prediction structures", false};
    Param<size_t> context_token_bits{"context_token_bits", "Width of the context token to use", 71};
    Param<size_t> max_uncommitted_takens{"max_uncommitted_takens", "Maximum number of uncommitted taken branches", 64};
    Param<bool> double_predict_known_nextline{
        "double_predict_known_nextline",
        "Produce an extra prediction per cycle if we know the next prediction must be nextline",
        false};
    Param<size_t> bank_bits{
        "bank_bits",
        "Number of bits used to specify the BTB/TAGE banks in the BPU (i.e. bank_bits=2 means 4 banks)",
        0};
    Param<size_t> bank_input_bits{"bank_input_bits", "Number of PC bits used to calculate the BTB/TAGE bank IDs", 8};
    Param<size_t> bank_start_bit{
        "bank_start_bit", "Lowest bit in the range of PC bits used to determine the bank IDs", 5};
    Param<bool> ignore_bank_conflict{
        "ignore_bank_conflict",
        "Allow two predictions to occur in the same cycle even if they have a bank conflict",
        false};
    Param<bool> one_indirect_per_cycle{"one_indirect_per_cycle",
                                       "Even in the face of multiple predictions per cycle, do not predict after "
                                       "making the first indirect prediction in a cycle",
                                       true};
    Param<bool> increment_on_bank_conflict{
        "increment_on_bank_conflict",
        "Avoid bank conflicts by incrementing the bank of the subsequent prediction if the same",
        true};
    Param<bool> reuse_banks_on_same_consistent_prediction{
        "reuse_banks_on_same_consistent_prediction",
        "Don't increment on a bank conflict if a clear/flush has caused us to continue working on the 'same consistent "
        "prediction' (i.e. processing the remainder of the same cacheline) as before the clear/flush",
        true};
    Param<bool> require_2_pending_commits{
        "require_2_pending_commits", "Require two pending commits to be waiting before committing", false};
    Param<bool> enable_per_cycle_pmu{"enable_per_cycle_pmu", "Output the PERCYC events (slows down panthera)", false};
    Param<bool> stash_only{"stash_only", "Operate as a Stashing Core", false};

    // FEQ-size-based frontend throttling parameters
    Param<bool> feq_throttling_enabled{"feq_throttling_enabled", "Enable frontend ohrottling via FEQ size", false};
    Param<size_t> feq_throttling_min_feq_size{
        "feq_throttling_min_feq_size", "Do not allow max FEQ size to go below this value", 12};
    Param<size_t> feq_throttling_cred_threshold{
        "feq_throttling_cred_threshold", "The counter value above and below which the max FEQ size will change", 4096};
    Param<size_t> feq_throttling_granularity{"feq_throttling_granularity",
                                             "When throttler counter goes above/below feq_throttling_cred_threshold, "
                                             "max FEQ size is decreased/increased by this many entries",
                                             4};
    Param<size_t> feq_throttling_epoch_length{
        "feq_throttling_epoch_length",
        "The epoch (in instructions) for counting IL1 misses and GPC flushes for throttling",
        4096};
    Param<size_t> feq_throttling_flush_cnt_lo{
        "feq_throttling_flush_cnt_lo", "Lower flush count threshold (separates *fsd_[012]_2 from *fsd_[012]_1)", 4};
    Param<size_t> feq_throttling_flush_cnt_hi{
        "feq_throttling_flush_cnt_hi", "Higher flush count threshold (separates *fsd_[012]_1 from *fsd_[012]_0)", 8};
    Param<size_t> feq_throttling_il1miss_cnt_lo{
        "feq_throttling_il1miss_cnt_lo",
        "Lower IL1 miss count threshold (separates *fsd_1_[012] from *fsd_2_[012])",
        64};
    Param<size_t> feq_throttling_il1miss_cnt_hi{
        "feq_throttling_il1miss_cnt_hi",
        "Higher IL1 miss count threshold (separates *fsd_0_[012] from *fsd_1_[012])",
        128};
    Param<size_t> feq_throttling_fsd_0_0{"feq_throttling_fsd_0_0",
                                         "Increase throttle counter by this value when >= "
                                         "feq_throttling_il1miss_cnt_hi && >= feq_throttling_flush_cnt_hi",
                                         100};
    Param<size_t> feq_throttling_fsd_0_1{
        "feq_throttling_fsd_0_1",
        "Increase throttle counter by this value when >= feq_throttling_il1miss_cnt_hi && >= "
        "feq_throttling_flush_cnt_lo && < feq_throttling_flush_cnt_hi",
        130};
    Param<size_t> feq_throttling_fsd_0_2{"feq_throttling_fsd_0_2",
                                         "Increase throttle counter by this value when >= "
                                         "feq_throttling_il1miss_cnt_hi && < feq_throttling_flush_cnt_lo",
                                         300};
    Param<size_t> feq_throttling_fsd_1_0{
        "feq_throttling_fsd_1_0",
        "Increase throttle counter by this value when >= feq_throttling_il1miss_cnt_lo && < "
        "feq_throttling_il1miss_cnt_hi && >= feq_throttling_flush_cnt_hi",
        20};
    Param<size_t> feq_throttling_fsd_1_1{
        "feq_throttling_fsd_1_1",
        "Increase throttle counter by this value when >= feq_throttling_il1miss_cnt_lo && < "
        "feq_throttling_il1miss_cnt_hi && >= feq_throttling_flush_cnt_lo && < feq_throttling_flush_cnt_hi",
        26};
    Param<size_t> feq_throttling_fsd_1_2{
        "feq_throttling_fsd_1_2",
        "Increase throttle counter by this value when >= feq_throttling_il1miss_cnt_lo && < "
        "feq_throttling_il1miss_cnt_hi && < feq_throttling_flush_cnt_lo",
        60};
    Param<size_t> feq_throttling_fsd_2_0{"feq_throttling_fsd_2_0",
                                         "Increase throttle counter by this value when < feq_throttling_il1miss_cnt_lo "
                                         "&& >= feq_throttling_flush_cnt_hi",
                                         10};
    Param<size_t> feq_throttling_fsd_2_1{"feq_throttling_fsd_2_1",
                                         "Increase throttle counter by this value when < feq_throttling_il1miss_cnt_lo "
                                         "&& >= feq_throttling_flush_cnt_lo && < feq_throttling_flush_cnt_hi",
                                         13};
    Param<size_t> feq_throttling_fsd_2_2{
        "feq_throttling_fsd_2_2",
        "Increase throttle counter by this value when < feq_throttling_il1miss_cnt_lo && < feq_throttling_flush_cnt_lo",
        30};

    Out<Fetch> prediction{"prediction_port"};
    Out<Fetch> clear{"clear"};
    In<Credit> feq_credit_return{"feq_credit_return"};
    In<uint> frontend_stall{"frontend_stall"};                      // IDR reports any frontend stalls last cycle
    In<uint> icache_miss_port{"icache_misses"};                     // ICF reports any icache misses
    In<uint> instructions_committed_port{"instructions_committed"}; // GPC reports committed instructions
    Out<FetchId> ittage_stall{"ittage_stall"};

    In<Uop> bob_alloc{"bob_alloc"};
    Out<Credit> bob_credit_return{"bob_credit_return"};

    In<Flush> br_check_flush{"br_check_flush"};
    In<Fetch> loop_fetch{
        "loop_fetch"}; // accept fake fetches created by ICF as a way to keep history updated while looping
    In<LoopRestart> loop_restart{"loop_restart"};
    Read<Flush> flush{"flush"};

    In<uint> branches_committed{"branches_committed"};

    // Only relevant for "oracle" style BPUs
    archx::DriverPort driver{"driver"};

private:
    struct OpObj : public sc::Transaction
    {
        OpObj() : initialized{false} {}
        OpObj(Fetch fetch) : initialized{true}, fetch{fetch} {}

        bool initialized{false};
        Fetch fetch;
        std::optional<Fetch> target_nextline_fetch;
        bool tage_changed_prediction{false};
        bool ittage_active{false};
    };
    using Op = transaction_ptr<OpObj>;

    enum class SleepStates {
        ASLEEP,
        AWAKE
    };

    using BOB = std::map<uint64_t, BOBEntry>;

    BOB bob;
    BranchHistory history{"history"};

    BP_TAGE tage{"tage"};
    NNP np{"np"};
    NNP nnp{"nnp"};
    NNP nnpp{"nnpp"};
    NNP nnnp{"nnnp"};

    uRSBEntry ursb[2];
    int ursb_tos{0};

    LoopDetector ld{"ld"};

    CreditPool feq{"feq"};

    Cycle last_fetch_cycle{0};
    SleepStates sleep_state{SleepStates::AWAKE};

    void init() override;
    void end_of_elaboration() override;
    bool run() override;
    void dump(std::ostream&) const override;

    void handle_feq_credit_returns();
    bool feq_can_acquire();
    void handle_throttler();
    void handle_loop_prediction_lockout();
    void handle_commits();
    bool handle_flush();
    bool handle_branch_check(bool gpc_flush);
    void handle_loop_history();
    void handle_loop_restart();
    void handle_allocations();

    void recover(Flush&, BOBEntry*);
    bool train(BOBEntry&);

    void flush_bob_entries(std::uint64_t alloc_id);

    void flush_history(Flush& flush_info,
                       BranchType branch_type,
                       ProgramCounter branch_pc,
                       std::optional<ProgramCounter> target,
                       bool taken);

    Event pc_mux_event{"pc_mux", "Front End Queue write, the pc_mux marks the start of the pipeline"};

    uint pending_commits{0};

    Event br_dispatch{"br_dispatch", "A branch uop is dispatched"};
    Event br_recover{"br_recover", "Recover from misprediction"};
    Event br_train{"br_train", "Branch predictor train"};
    Event np_predict{"np_predict", "Prediction from the NP"};
    Event nnp_predict{"nnp_predict", "Prediction from the NNP"};
    Event nnpp_predict{"nnpp_predict", "Prediction from the NNPP"};
    Event nnnp_predict{"nnnp_predict", "Prediction from the NNNP"};
    Event btb_predict{"btb_predict", "Prediction from the L1 BTB"};
    Event double_nextline_predict{
        "double_nextline_predict",
        "Injecting an extra prediction because we know it does not contain branches and will be nextline"};
    Event ittage_predict{"ittage_predict", "Prediction from the ITTAGE"};
    Event nnp_train{"nnp_train", "Train the NNP from the L1 BTB"};
    Event ev_pipeline_mispredict{"pipeline_mispredict", "Branch mispredicted due to training delay"};
    Event asleep{"asleep", "BPU is asleep, no new predictions this cycle"};
    Event pmu_event{"pmu", "A PMU event has occurred"};
    Event ev_looping{"looping", "The BPU is sleeping while in a loop"};
    Event ev_bhr_full_stall{"bhr_full_stall", "The BPU cannot predict because the BHR is 'full'"};
    Event ev_address_stall{
        "address_stall",
        "The BPU is not predicting because of an invalid address (either non-canonical or unknown target)"};
    EventData<archx::PMU> pmu_num_data{"event", "PMU event number"};
    EventData<size_t> idle_cycles_data{"idle_cycles", "Number of consecutive cycles that BPU was idle"};
    EventData<bool> br_taken{"br_taken", "Branch was taken"};
    EventData<ProgramCounter> entrance_pc{"entrance_pc", "entrance PC of prediction"};
    EventData<ProgramCounter> br_pc{"br_pc", "PC of branch instruction"};
    EventData<ProgramCounter> br_target{"br_target", "Branch target"};
    EventData<bool> br_target_nextline{"br_target_nextline", "Branch target's a nextline prediction"};
    EventData<ProgramCounter> br_prev_pc{"br_prev_pc", "Previous cache line PC"};
    EventData<BranchType> br_type{"br_type", "Branch type"};
    EventData<std::string> br_line_prediction{"br_line_prediction", "Predictions for each instruction in a line"};
    EventData<bool> br_mispredicted{"br_mispredicted", "Branch was mispredicted (either direction or target)"};
    EventData<bool> br_mispredicted_direction{"br_mispredicted_direction", "Branch direction was mispredicted"};
    EventData<bool> br_mispredicted_target{"br_mispredicted_target", "Branch target was mispredicted"};
    EventData<std::string> br_primary_predictor{"br_primary_predictor", "Primary predictor of a branch"};
    EventData<std::string> br_alternate_predictor{"br_alternate_predictor", "Alternate predictor of a branch"};
    EventData<bool> br_use_alternate{"br_use_alternate", "Alternate prediction was used"};
    EventData<bool> br_useful{"br_useful", "global prediction was useful (corrected the alternate)"};
    EventData<bool> br_btb_miss{"br_btb_miss", "prediction missed the BTB"};
    EventData<bool> br_rsb_hit{"br_rsb_hit", "prediction hit the BTB"};
    EventData<bool> br_ittage_hit{"br_ittage_hit", "prediction hit the indirect TAGE"};
    EventData<int> br_update_type{"br_update_type", "0: no update; 1: 1 cycle update; 3: 3 cycle RMW update"};
    EventData<bool> br_miss_all{"br_miss_all", "branch missed all BPU arrays"};
    EventData<std::string> br_history{"br_history", "history of PCs as a comma seperated string"};
    EventData<std::string> cbhr{"cbhr", "compressed history as a hexadecimal string"};
    EventData<EarlyPredictor> br_early_predictor{"early_predictor",
                                                 "Which early predictor was used for this prediction"};

    TokenGenerator token_generator;
    BranchContext context;
    ProgramCounter pc{0};
    ProgramCounter prev_pc{0};
    ProgramCounter prev_pc_uncorrected{0}; // this is prev_pc, but doesn't get 'corrected' upon flush
    ProgramCounter prev_prev_pc{0};
    ProgramCounter prev_prev_pc_uncorrected{0}; // this is prev_prev_pc, but doesn't get 'corrected' upon flush
    bool reuse_bank{false};
    BPUBank prev_bank{0}; // this is the bank used by the previous prediction
    FetchId current_fetch_id{1};
    int update_state{0};
    bool pc_is_valid{false};
    bool clearing{false};
    EarlyPredictor next_early_predictor{EarlyPredictor::NP};
    // Holds invalid fetch address if we are stalling because of it
    std::optional<ProgramCounter> stalling_fetch_address{std::nullopt};

    // variables associated with FEQ size-based frontend throttling
    size_t feq_throttled_size{0}; // The current max FEQ size
    int64_t feq_throttle_counter{0};
    unsigned feq_throttle_fsd_value{
        0}; // Frontend Stall Decrease - the amount to subtract from feq_throttle_counter per frontend stall
    unsigned feq_throttle_epoch_instrs{0};  // cycles counted so far in the current epoch
    unsigned feq_throttle_icache_misses{0}; // icache misses counted this epoch
    unsigned feq_throttle_flushes{0};       // GPC flushes counted this epoch
    bool feq_throttle_flush_shadow{false};  // Are we in the shadow of a flush?

    // variables associated with loop prediction lockout
    Cycle loop_prediction_lockout_epoch_end{0};
    Cycle loop_prediction_lockout_end{0};

    bool mispredicted_el0_path{false};

    bool update_context();
    void update_pc(ProgramCounter new_pc, bool force = false);
    ProgramCounter consume_pc();
    BPUBank determine_bank(ProgramCounter prediction_entry) const;
    void send_fetch(Fetch& fetch);

    void update_history(Fetch& fetch);

    bool fetch_stage(Pipeline<Op>::Data data);
    bool fetch_stage_ran_this_cycle{false};
    bool nnp_stage_saw_indirect_this_cycle{false};
    bool nnp_stage(Pipeline<Op>::Data data);
    bool btb_stage(Pipeline<Op>::Data data);
    bool btb_tage_only_stage(Pipeline<Op>::Data data);
    bool ittage_stall_stage(Pipeline<Op>::Data data);
    bool ittage_clear_stage(Pipeline<Op>::Data data);
    void update_nnp(Fetch& fetch, bool useful);
    int get_update_state(BOBEntry& bob_entry) const;
    void do_update();
    bool nextline_predict_on_clear(Pipeline<Op>::Data data, Fetch& fetch);

    PipelineGroup<Op> prediction_pipe{"prediction_pipe",
                                      {AMPERE_PIPE_STAGE_M(BPU, fetch_stage),
                                       AMPERE_PIPE_STAGE_M(BPU, nnp_stage),
                                       AMPERE_PIPE_STAGE_M(BPU, btb_stage)}};

    PipelineGroup<Op> prediction_late_ittage_pipe{"prediction_late_ittage",
                                                  {AMPERE_PIPE_STAGE_M(BPU, fetch_stage),
                                                   AMPERE_PIPE_STAGE_M(BPU, nnp_stage),
                                                   AMPERE_PIPE_STAGE_M(BPU, btb_tage_only_stage),
                                                   AMPERE_PIPE_STAGE_M(BPU, ittage_stall_stage),
                                                   AMPERE_PIPE_STAGE_M(BPU, ittage_clear_stage)}};

    PipelineGroup<Op> prediction_perfect_nnp_pipe{"prediction_perfect_nnp_pipe",
                                                  {
                                                      AMPERE_PIPE_STAGE_M(BPU, fetch_stage),
                                                      AMPERE_PIPE_STAGE_M(BPU, btb_stage),
                                                  }};

    Fetch predict_perfectly(ProgramCounter current_pc);
    void lookup_ursb(Fetch& fetch, const NNPEntry& nnp_entry);

    std::string predictor_name(const BranchInfo& br_info, bool alternate) const;
};

} // namespace ampere::core

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
