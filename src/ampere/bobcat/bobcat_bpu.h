//
// Copyright (c) 2018-2024 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include <limits>

#include "ampere/archx/branch_type.h"
#include "ampere/archx/driver.h"
#include "ampere/archx/inum.h"
#include "ampere/core/bp_tage.h"
#include "ampere/core/branch_context.h"
#include "ampere/core/branch_history.h"
#include "ampere/core/fetch.h"
#include "ampere/core/nnp.h"
#include "ampere/sc/sc.h"
#include "ampere/util/enum.h"

#define BPU_DEBUG(fmt, ...) \
    AMPERE_UNCATEGORIZED_DEBUG("[{}] {:8} " fmt, sc_core::sc_time_stamp(), basename(), __VA_ARGS__)

namespace ampere::bobcat {

using namespace ampere::sc;
using ampere::archx::ProgramCounter;

class BobcatBPU : public Module
{
public:
    explicit BobcatBPU(const sc_core::sc_module_name& name) : Module{name}, tage("tage", bank_bits) {}

    archx::DriverPort driver{"driver"};

    Param<std::uint64_t> max_instructions{
        "max_instructions", "Maximum number of instructions to simulate", std::numeric_limits<std::uint64_t>::max()};
    Param<std::string> save_warmup_file{"save_warmup_file", "Path at which to save BPU warmup JSON file", ""};
    Param<std::string> restore_warmup_file{"warmup_file", "Path at which to restore BPU warmup JSON file", ""};
    Param<bool> np_on_flush{"np_on_flush",
                            "Use and train NP instead of NNP for first prediction following any GPC flush or BTC clear",
                            false};
    Param<bool> double_prediction{
        "double_prediction",
        "Use structures as if we were doing double prediction akin to the full panthera model's",
        false};
    Param<bool> use_context_token{
        "use_context_token", "XOR a context token into tags of indirect prediction structures", false};
    Param<size_t> context_token_bits{"context_token_bits", "Width of the context token to use", 71};
    Param<size_t> bank_bits{
        "bank_bits",
        "Number of bits used to specify the BTB/TAGE banks in the BPU (i.e. bank_bits=2 means 4 banks)",
        0};
    Param<size_t> bank_input_bits{"bank_input_bits", "Number of PC bits used to calculate the BTB/TAGE bank IDs", 8};
    Param<size_t> bank_start_bit{
        "bank_start_bit", "Lowest bit in the range of PC bits used to determine the bank IDs", 5};
    Param<bool> increment_on_bank_conflict{
        "increment_on_bank_conflict",
        "Avoid bank conflicts by incrementing the bank of the subsequent prediction if the same",
        true};
    Param<bool> reuse_banks_on_same_consistent_prediction{
        "reuse_banks_on_same_consistent_prediction",
        "Don't increment on a bank conflict if a clear/flush has caused us to continue working on the 'same consistent "
        "prediction' (i.e. processing the remainder of the same cacheline) as before the clear/flush",
        true};

    void print_stats(const std::string& trace);
    void print_stats_csv(const std::string& trace, std::ostream& os);

private:
    core::BP_TAGE tage;
    core::BranchHistory history{"history"};
    core::NNP np{"np"};
    core::NNP nnpp{"nnpp"};
    core::NNP nnp{"nnp"};
    core::NNP nnnp{"nnnp"};

    void init() override;
    void end_of_elaboration() override;
    bool run() override;

    void update_nnp(core::Fetch& fetch, bool useful);
    void execute_instructions(core::Fetch& fetch);
    bool execute_branch(core::Fetch& fetch, core::Instruction instruction);
    bool branch_target_nextline(std::shared_ptr<archx::Instruction> archx_instruction, core::BranchInfo& br_info);
    void update_stats(const core::BranchInfo& br_info, bool btc_clear);
    bool update_context();
    void flush_history(core::Flush& flush_info,
                       archx::BranchType branch_type,
                       ProgramCounter branch_pc,
                       ProgramCounter target,
                       bool taken);
    core::BPUBank determine_bank(ProgramCounter prediction_entry) const;

    Event br_recover{"br_recover", "Recover from misprediction"};
    Event br_train{"br_train", "Branch predictor train"};
    Event np_predict{"np_predict", "NP prediction"};
    Event nnpp_predict{"nnpp_predict", "NNPP prediction"};
    Event nnp_predict{"nnp_predict", "NNP prediction"};
    Event nnnp_predict{"nnnp_predict", "NNNP prediction"};
    Event btb_predict{"btb_predict", "Prediction from the L1 BTB"};
    Event btb_clear{"btb_clear", "TAGE/BTB prediction disagreed with NP/NNP"};
    Event btc_clear{"btc_clear", "Branch check clear"};
    Event instruction_commit{"instruction_commit", "Instruction committed"};
    EventData<archx::inum_t> ev_inum{"inum", "Instruction ID"};
    EventData<bool> br_taken{"br_taken", "Branch was taken"};
    EventData<ProgramCounter> br_pc{"br_pc", "PC of branch instruction"};
    EventData<ProgramCounter> br_target{"br_target", "Branch target"};
    EventData<archx::BranchType> br_type{"br_type", "Branch type"};
    EventData<std::string> br_line_prediction{"br_line_prediction", "Predictions for each instruction in a line"};
    EventData<bool> br_mispredicted{"br_mispredicted", "Branch was mispredicted (either direction or target)"};
    EventData<bool> br_mispredicted_direction{"br_mispredicted_direction", "Branch direction was mispredicted"};
    EventData<bool> br_mispredicted_target{"br_mispredicted_target", "Branch target was mispredicted"};
    EventData<std::string> br_primary_predictor{"br_primary_predictor", "Primary predictor of a branch"};
    EventData<std::string> br_alternate_predictor{"br_alternate_predictor", "Alternate predictor of a branch"};
    EventData<bool> br_use_alternate{"br_use_alternate", "Alternate prediction was used"};
    EventData<bool> br_useful{"br_useful", "global prediction was useful (corrected the alternate)"};
    EventData<std::string> br_history{"br_history", "history of PCs as a comma seperated string"};
    EventData<std::string> cbhr{"cbhr", "compressed history as a hexadecimal string"};
    EventData<bool> br_btb_miss{"br_btb_miss", "prediction missed the BTB"};
    EventData<ProgramCounter> pc_data{"pc", "Program Counter"};

    core::TokenGenerator token_generator;
    core::BranchContext context;
    ProgramCounter pc{0};
    ProgramCounter prev_pc{0};
    ProgramCounter prev_pc_uncorrected{0};
    ProgramCounter prev_prev_pc{0};
    ProgramCounter prev_prev_pc_uncorrected{0};
    bool reuse_bank{false};
    core::BPUBank prev_bank{0};
    core::FetchId current_fetch_id{1};
    std::uint64_t current_alloc_id{0};

    core::EarlyPredictor next_early_predictor{core::EarlyPredictor::NP};

    std::string predictor_name(const core::BranchInfo& br_info, bool alternate) const;

    struct Statistics
    {
        std::uint64_t count{0};
        std::uint64_t taken{0};
        std::uint64_t mispredicted{0};
        std::uint64_t mispredicted_direction{0};
        std::uint64_t mispredicted_target{0};
        std::uint64_t btc_clear{0};
        std::uint64_t btb_clear{0};
    };

    std::array<Statistics, util::enum_size<core::BranchType>()> stats;
    std::uint64_t btb_clears{0};
    std::uint64_t instructions{0};
    bool measuring{false};

    Statistics total_statistics() const;

    void print_conditional_branch_stats(const std::string& title, const Statistics& st);
    void print_direct_branch_stats(const std::string& title, const Statistics& st);
    void print_indirect_branch_stats(const std::string& title, const Statistics& st);
};

class BobcatCore : public Module
{
public:
    BobcatCore(const sc_core::sc_module_name& name) : Module{name} {}
    Param<std::vector<std::string>> decoders{
        "decoders", "list of decoder names, in the order they should be checked", "[]"_json};

    ampere::bobcat::BobcatBPU bpu{"bpu"};

    void end_of_elaboration() override
    {
        if (decoders.val().empty())
            AMPERE_ERROR("no decoders were specified");

        archx::Instruction::set_decoders(decoders);
    }
};

class BobcatTop : public Module
{
public:
    BobcatTop(const sc_core::sc_module_name& name, Clock* clk, Environment* env) : Module{name, clk, env} {}
    void set_driver(ampere::archx::Driver& driver) { core.bpu.driver(driver); }
    void print_stats(const std::string& trace) { core.bpu.print_stats(trace); }
    void print_stats_csv(const std::string& trace, std::ostream& os) { core.bpu.print_stats_csv(trace, os); }

private:
    ampere::bobcat::BobcatCore core{"core"};
};

} // namespace ampere::bobcat

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
