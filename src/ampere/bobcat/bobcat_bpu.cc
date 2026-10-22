//
// Copyright (c) 2018-2024 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/bobcat/bobcat_bpu.h"
#include "ampere/archx/branch_type.h"
#include "ampere/archx/flush_type.h"
#include "ampere/archx/branch_target.h"
#include "ampere/core/branch_check.h"
#include "ampere/core/flush.h"
#include "ampere/core/nnp.h"
#include "ampere/util/enum.h"

namespace ampere::bobcat {

using namespace ampere::archx;
using namespace ampere::sc;

static std::string line_prediction_as_string(const core::LinePrediction& line_prediction);

void BobcatBPU::init()
{
    pc = driver->next_pc();

    for (auto& hist : tage.get_conditional_histories())
        history.add_history_size(hist);
}

void BobcatBPU::end_of_elaboration()
{
    token_generator = core::TokenGenerator(context_token_bits);
    context = core::BranchContext{driver->exception_level(), driver->asid(), token_generator.next(!use_context_token)};

    if (!restore_warmup_file.val().empty())
        tage.restore_state(restore_warmup_file.val(), context);
}

bool BobcatBPU::run()
{
    auto fetch = make<core::Fetch>(pc, current_fetch_id++);
    fetch->bank = determine_bank(pc);

    fetch->prev_prev_program_counter = prev_prev_pc;
    fetch->prev_prev_program_counter_uncorrected = prev_prev_pc_uncorrected;
    fetch->prev_program_counter = prev_pc;
    fetch->prev_program_counter_uncorrected = prev_pc_uncorrected;
    fetch->early_predictor = next_early_predictor;
    prev_prev_pc = prev_pc;
    prev_prev_pc_uncorrected = prev_pc_uncorrected;
    prev_pc = fetch->program_counter;
    prev_pc_uncorrected = fetch->program_counter;
    reuse_bank = false;
    prev_bank = fetch->bank;

    // First, perform the NP/NNP prediction

    boost::optional<core::NNPEntry&> hit;
    if (fetch->early_predictor == core::EarlyPredictor::NP)
        hit = np.lookup(fetch);
    else if (fetch->early_predictor == core::EarlyPredictor::NNPP)
        hit = nnpp.lookup(fetch);
    else if (fetch->early_predictor == core::EarlyPredictor::NNP)
        hit = nnp.lookup(fetch);
    else if (fetch->early_predictor == core::EarlyPredictor::NNNP)
        hit = nnnp.lookup(fetch);
    else
        AMPERE_FATAL("Unexpected EarlyPredictor value in fetch->early_predictor");

    if (hit) {
        fetch->update_prediction({hit->branch_type, fetch->cache_line() + (hit->offset * 4), hit->target});
    }

    // If the current N*P* prediction is an indirect, or if we are not
    // approximating double-prediction, the next prediction must be from the
    // NNP. Otherwise, we follow a state machine of sorts to help us alternate
    // between the NNP and NNNP.
    if (is_indirect(fetch->get_prediction().branch_type) || !double_prediction) {
        next_early_predictor = core::EarlyPredictor::NNP;
    } else if (next_early_predictor == core::EarlyPredictor::NNNP || next_early_predictor == core::EarlyPredictor::NNPP)
    {
        next_early_predictor = core::EarlyPredictor::NNP;
    } else if (next_early_predictor == core::EarlyPredictor::NNP) {
        next_early_predictor = double_prediction ? core::EarlyPredictor::NNNP : core::EarlyPredictor::NNP;
    } else {
        AMPERE_ASSERT(next_early_predictor == core::EarlyPredictor::NP, "Expected NP");
        next_early_predictor = double_prediction ? core::EarlyPredictor::NNPP : core::EarlyPredictor::NNP;
    }

    if (fetch->early_predictor == core::EarlyPredictor::NP)
        np_predict(fetch,
                   fetch->program_counter,
                   br_type(fetch->get_prediction().branch_type),
                   br_pc(fetch->get_prediction().branch_pc),
                   br_target(fetch->get_prediction().target),
                   br_taken(fetch->get_prediction().is_taken()));
    else if (fetch->early_predictor == core::EarlyPredictor::NNPP)
        nnpp_predict(fetch,
                     fetch->program_counter,
                     br_type(fetch->get_prediction().branch_type),
                     br_pc(fetch->get_prediction().branch_pc),
                     br_target(fetch->get_prediction().target),
                     br_taken(fetch->get_prediction().is_taken()));
    else if (fetch->early_predictor == core::EarlyPredictor::NNP)
        nnp_predict(fetch,
                    fetch->program_counter,
                    br_type(fetch->get_prediction().branch_type),
                    br_pc(fetch->get_prediction().branch_pc),
                    br_target(fetch->get_prediction().target),
                    br_taken(fetch->get_prediction().is_taken()));
    else
        nnnp_predict(fetch,
                     fetch->program_counter,
                     br_type(fetch->get_prediction().branch_type),
                     br_pc(fetch->get_prediction().branch_pc),
                     br_target(fetch->get_prediction().target),
                     br_taken(fetch->get_prediction().is_taken()));

    // Next, perform the BTB/TAGE/ITTAGE/RSB predictions which "correct" the
    // NP/NNP predictions via BTB clears

    core::Prediction old_prediction = fetch->get_prediction();
    tage.predict(fetch, history, context);

    btb_predict(fetch,
                core::fetch_id_data(fetch->fetch_id),
                fetch->program_counter,
                br_type(fetch->get_prediction().branch_type),
                br_pc(fetch->get_prediction().branch_pc),
                br_target(fetch->get_prediction().target),
                br_taken(fetch->get_prediction().is_taken()),
                br_line_prediction(line_prediction_as_string(fetch->line_prediction)));

    if (prediction_changed(old_prediction, fetch->get_prediction())) {
        btb_clear(fetch,
                  fetch->program_counter,
                  br_type(fetch->get_prediction().branch_type),
                  br_pc(fetch->get_prediction().branch_pc),
                  br_target(fetch->get_prediction().target),
                  br_taken(fetch->get_prediction().is_taken()));
        btb_clears++;
        update_nnp(fetch, false /*useful*/);

        // Next early predictor is always NNP upon BTB clear
        next_early_predictor = core::EarlyPredictor::NNP;
    } else {
        update_nnp(fetch, true /*useful*/);
    }

    // Update the branch history according to any taken branches in the
    // prediction we just made

    if (is_conditional(fetch->get_prediction().branch_type) && fetch->get_prediction().is_taken())
        history.push(fetch->fetch_id, fetch->get_prediction().branch_pc);
    else if (is_indirect(fetch->get_prediction().branch_type))
        history.push(fetch->fetch_id, fetch->get_prediction().branch_pc, fetch->get_prediction().target);
    else if (is_unconditional(fetch->get_prediction().branch_type))
        history.push(fetch->fetch_id, fetch->get_prediction().branch_pc);

    // Finally, "execute" the instructions, finding and handling any BTC
    // clears (branch checks) or branch mispredictions

    execute_instructions(fetch);

    return true;
}

void BobcatBPU::update_nnp(core::Fetch& fetch, bool useful)
{
    if (fetch->early_predictor == core::EarlyPredictor::NP)
        np.update(fetch, useful);
    else if (fetch->early_predictor == core::EarlyPredictor::NNPP)
        nnpp.update(fetch, useful);
    else if (fetch->early_predictor == core::EarlyPredictor::NNP)
        nnp.update(fetch, useful);
    else
        nnnp.update(fetch, useful);
}

void BobcatBPU::execute_instructions(core::Fetch& fetch)
{
    bool end_fetch = false;

    for (size_t i = 0; i < fetch->num_instructions() && !end_fetch; i++) {
        auto archx_instruction = driver->fetch(pc);
        auto next_pc = archx_instruction->next_program_counter();

        bool context_updated = update_context();
        bool sequential = archx_instruction->next_program_counter() == pc + 4;
        bool is_last_instruction = (i == fetch->num_instructions() - 1);
        bool predicted_taken = fetch->get_prediction().is_taken();
        auto instruction = trans_make<core::Instruction>(archx_instruction, fetch);

        if (archx_instruction->is_branch() || (is_last_instruction && predicted_taken)) {
            end_fetch = execute_branch(fetch, instruction);
        } else if (!sequential || context_updated) {
            // This is a non-sequential instruction that is not a branch (fault, EL change, whatever).
            auto flush = make_child<core::Flush>(fetch, next_pc, pc, fetch, instruction, 0, core::FlushType::NOT_KNOWN);
            flush->is_context_change = context_updated;
            tage.recover(flush, nullptr, history, context);
            history.flush(fetch->fetch_id);
            next_early_predictor = core::EarlyPredictor::NP;
            end_fetch = true;
        }

        // Calculate whether we should reuse this prediction's bank for the
        // next prediction, assuming the current instruction is the last
        // instruction (we recalculate this for each instruction so it is fresh
        // if/when we fall off the end of this loop). We re-use the bank if the
        // next instruction will be to the "same consistent prediction" - in
        // other words, if it could have been predicted using the same
        // prediction if the BPU had been more accurate.
        bool same_consistent_prediction =
            sequential && !archx_instruction->is_taken_branch() && next_pc.line_offset() != 0 && !context_updated;
        reuse_bank = same_consistent_prediction;

        driver->commit(archx_instruction);
        instruction_commit(
            instruction, pc_data(archx_instruction->program_counter()), ev_inum(archx_instruction->inum()));
        pc = next_pc;

        if (measuring)
            instructions++;
        if (instructions >= max_instructions
            || (archx_instruction->control()
                && *archx_instruction->control() == archx::SimulationControl::STOP_MEASUREMENT))
        {
            if (!save_warmup_file.val().empty())
                tage.save_state(save_warmup_file, context);

            AMPERE_INFO(
                "Stop performance measurement after {} instruction @ {}", instructions, sc_core::sc_time_stamp());
            sc_core::sc_stop();
            break;
        }

        if (archx_instruction->control()
            && *archx_instruction->control() == archx::SimulationControl::START_MEASUREMENT)
        {
            instructions = 0;
            measuring = true;
            for (auto& st : stats)
                st = Statistics{};
            btb_clears = 0;
            get_event_stream()->enable();
        }
    }
}

/*
 * Return true iff we can determine from the instruction stream that the
 * cacheline of the target of the branch referenced by
 * archx_instruction/br_info does not contain any branches between the target
 * and the end of its line.
 */
bool BobcatBPU::branch_target_nextline(std::shared_ptr<archx::Instruction> archx_instruction, core::BranchInfo& br_info)
{
    auto next_cacheline_start = br_info.target.next_line_address();
    auto next_sequential_pc = br_info.target;
    auto next_driver_instruction = driver->fetch(next_sequential_pc);

    while (!next_driver_instruction->wrong_path() && !next_driver_instruction->is_branch()
           && next_sequential_pc != next_cacheline_start)
    {
        next_sequential_pc += 4;
        next_driver_instruction = driver->fetch(next_sequential_pc);
    }

    driver->recover(archx_instruction->inum());

    return next_sequential_pc == next_cacheline_start;
}

void BobcatBPU::flush_history(
    core::Flush& flush_info, BranchType branch_type, ProgramCounter branch_pc, ProgramCounter target, bool taken)
{
    history.flush(flush_info->fetch->fetch_id);

    if (is_conditional(branch_type) && taken)
        history.push(flush_info->fetch->fetch_id, branch_pc);
    else if (is_indirect(branch_type))
        history.push(flush_info->fetch->fetch_id, branch_pc, target);
    else if (is_unconditional(branch_type))
        history.push(flush_info->fetch->fetch_id, branch_pc);
}

/*
 * Determine the BPU bank based on the previous bank (reusing it if indicated),
 * and the current banking scheme.
 */
core::BPUBank BobcatBPU::determine_bank(ProgramCounter prediction_entry) const
{
    if (!bank_bits)
        return 0;
    else if (reuse_banks_on_same_consistent_prediction && reuse_bank)
        return prev_bank;

    // First, calculate the bank based on the prediction PC alone
    core::BPUBank desired_bank = core::fold_to_n_bits(
        prediction_entry.extract_range(bank_start_bit + bank_input_bits - 1, bank_start_bit), bank_bits);

    // If applicable, modify the bank using any bank conflict avoidance schemes
    // in place
    if (increment_on_bank_conflict && desired_bank == prev_bank)
        return (desired_bank + 1) & ((1UL << bank_bits) - 1);
    else
        return desired_bank;
}

/*
 * "Executing" a branch in bobcat means "figure out if we need to end the fetch
 * early". So it encompasses both branch check logic traditionally performed in
 * the frontend as well as the misprediction traditionally performed in the
 * backend. The return value is a combined signal that, if true, means the
 * current fetch needs to end "early" with the current instruction (a new
 * prediction/fetch needs to be made).
 */
bool BobcatBPU::execute_branch(core::Fetch& fetch, core::Instruction instruction)
{
    // First, perform branch checks to catch cases where we can correct the
    // original prediction in the frontend based on information from the
    // instruction bytes.
    auto btc_flush = branch_check(fetch, instruction);

    auto archx_instruction = instruction->archx_instruction;
    auto branch_type = archx_instruction->branch_type();

    if (btc_flush) {
        auto pc = instruction->pc();
        auto target = fetch->get_prediction().is_taken() ? fetch->get_prediction().target : pc + 4;
        auto flush = make_child<core::Flush>(instruction, target, pc, fetch, instruction, 0, *btc_flush);
        bool taken = flush->type != FlushType::BRANCH_CHECK_WRONG_TYPE;

        flush_history(flush, branch_type, pc, target, taken);
        tage.recover(flush, nullptr, history, context);
        btc_clear(instruction,
                  core::fetch_id_data(fetch->fetch_id),
                  fetch->program_counter,
                  br_type(fetch->get_prediction().branch_type),
                  br_pc(fetch->get_prediction().branch_pc),
                  br_target(fetch->get_prediction().target),
                  br_taken(fetch->get_prediction().is_taken()),
                  br_line_prediction(line_prediction_as_string(fetch->line_prediction)));

        next_early_predictor = core::EarlyPredictor::NP;
    }

    // Initialization of br_info occurs after both the calls to the branch
    // check logic and tage.recover() because both can modify the prediction.
    core::BranchInfo br_info{branch_type, pc, fetch, current_alloc_id++};

    // 'Execute' the branch, detecting any mispredictions (or exceptions) and
    // flushing/recovering as needed

    // `actually_taken` is our best guess about whether the branch was taken
    // based on the instruction stream (but could be wrong in the case of
    // exceptions)
    bool actually_taken = archx_instruction->is_taken_branch();

    if (actually_taken != br_info.taken) {
        br_info.taken = actually_taken;
        br_info.mispredicted_direction = true;
    }

    if (actually_taken) {
        if (is_direct(br_info.type)) {
            auto target = branch_target_if_taken(br_info.pc, archx_instruction->opcode());
            if (br_info.target != target) {
                br_info.mispredicted_target = true;
                br_info.target = target;
            }
        } else if (br_info.target != archx_instruction->next_program_counter()) {
            br_info.mispredicted_target = true;
            br_info.target = archx_instruction->next_program_counter();
        }

        br_info.target_nextline = branch_target_nextline(archx_instruction, br_info);
    }

    bool is_mispredict = br_info.mispredicted_direction || br_info.mispredicted_target;
    // If our corrected prediction target still does not match the next PC
    // (after fixing up any mispredicted branches), it probably means we took
    // an exception. Flush in this case as well.
    bool is_exception = (br_info.target != archx_instruction->next_program_counter());

    if (is_mispredict || is_exception) {
        auto target = is_mispredict ? br_info.target : archx_instruction->next_program_counter();
        auto flush =
            make_child<core::Flush>(fetch,
                                    target,
                                    pc,
                                    fetch,
                                    instruction,
                                    0,
                                    is_mispredict ? core::FlushType::BR_MISPREDICT : core::FlushType::NOT_KNOWN);
        flush_history(flush, branch_type, pc, target, br_info.taken);
        tage.recover(flush, &br_info, history, context);

        prev_pc = fetch->prev_program_counter;
        prev_prev_pc = fetch->prev_prev_program_counter;

        br_recover(instruction, br_pc(br_info.pc), br_target(archx_instruction->next_program_counter()));

        next_early_predictor = core::EarlyPredictor::NP;
    }

    // Commit
    tage.train(br_info, history, context);
    tage.commit_branch(br_info);

    bool useful = br_info.slot.primary.taken == br_info.taken && br_info.slot.alternate.taken != br_info.taken;

    if (br_train.is_enabled()) {
        std::string history_string{};
        std::string compressed_history_string{};

        {
            auto predictor =
                (br_info.slot.use_alternate ? br_info.slot.alternate.predictor : br_info.slot.primary.predictor);
            auto pred_obj = dynamic_cast<core::BPGlobal*>(predictor);
            unsigned depth = pred_obj ? pred_obj->history_size + 1 : 2;

            history_string = history.history_as_string(br_info.fetch->fetch_id, depth);
            compressed_history_string = history.compressed_history_as_string(br_info.fetch->fetch_id);
        }

        br_train(instruction,
                 br_info.pc,
                 br_type(br_info.type),
                 br_target(br_info.target),
                 br_taken(br_info.taken),
                 br_btb_miss(br_info.btb_miss),
                 br_mispredicted(br_info.mispredicted_direction || br_info.mispredicted_target),
                 br_mispredicted_direction(br_info.mispredicted_direction),
                 br_mispredicted_target(br_info.mispredicted_target),
                 br_primary_predictor(predictor_name(br_info, false)),
                 br_alternate_predictor(predictor_name(br_info, true)),
                 br_use_alternate(br_info.slot.use_alternate),
                 br_useful(useful),
                 br_history(history_string),
                 cbhr(compressed_history_string));
    }

    update_stats(br_info, btc_flush.has_value());
    if (actually_taken)
        history.commit(fetch->fetch_id);

    return is_mispredict || is_exception || btc_flush.has_value();
}

/*
 * Update the branch context, returning whether the non-EL context was updated.
 * In other words return false iff no context was changed, or only EL was
 * changed. This would ideally also comprehend VMID, TR, NS, SCXTNUM_ELx.
 */
bool BobcatBPU::update_context()
{
    archx::EL el = driver->exception_level();
    uint16_t asid = driver->asid();
    bool context_change = (asid != context.asid);

    if (context_change)
        context = core::BranchContext{el, asid, token_generator.next(!use_context_token)};
    else if (el != context.el)
        context = core::BranchContext{el, asid, context.token};

    return context_change;
}

void BobcatBPU::update_stats(const core::BranchInfo& br_info, bool btc_clear)
{
    auto& st = stats[br_info.type];

    bool miss_dir{false}, miss_tgt{false};

    ++st.count;

    if (br_info.taken)
        ++st.taken;

    if (br_info.mispredicted_direction) {
        ++st.mispredicted_direction;
        miss_dir = true;
    }

    if (br_info.mispredicted_target) {
        ++st.mispredicted_target;
        miss_tgt = true;
    }

    if (btc_clear)
        ++st.btc_clear;

    if (miss_dir || miss_tgt)
        ++st.mispredicted;
}

std::string BobcatBPU::predictor_name(const core::BranchInfo& br_info, bool alternate) const
{
    auto predictor = alternate ? br_info.slot.alternate.predictor : br_info.slot.primary.predictor;

    if (!predictor) {
        return "none";
    } else {
        auto pred_obj = dynamic_cast<Module*>(predictor);
        assert(pred_obj);
        return pred_obj->name();
    }
}

static std::string line_prediction_as_string(const core::LinePrediction& line_prediction)
{
    std::ostringstream oss;

    for (auto& pred : line_prediction) {
        if (is_conditional(pred.branch_type)) {
            bool taken = pred.use_alternate ? pred.alternate.taken : pred.primary.taken;

            if (taken)
                oss << (pred.use_alternate ? 't' : 'T');
            else
                oss << (pred.use_alternate ? 'n' : 'N');
        } else {
            oss << branch_type_char(pred.branch_type);
        }
    }

    return oss.str();
}

void BobcatBPU::print_conditional_branch_stats(const std::string& title, const Statistics& st)
{
    fmt::print(
        "\n-------------------------------------------\n"
        "{:<32} {:>10}\n"
        "{:<32} {:>10}\n"
        "{:<32} {:>10}\n"
        "{:<32} {:>10}\n"
        "{:<32} {:>10}\n"
        "{:<32} {:>10}\n"
        "{:<32} {:>10.2f}%\n"
        "{:<32} {:>10.2f}\n",
        title + ":",
        st.count,
        "taken branches:",
        st.taken,
        "mispredicts direction:",
        st.mispredicted_direction,
        "mispredicts target:",
        st.mispredicted_target,
        "mispredicts total:",
        st.mispredicted,
        "btc corrections:",
        st.btc_clear,
        "misprediction rate:",
        (float)st.mispredicted / st.count * 100.0,
        "mpki:",
        (float)st.mispredicted / instructions * 1000.0);
}

void BobcatBPU::print_direct_branch_stats(const std::string& title, const Statistics& st)
{
    fmt::print(
        "\n-------------------------------------------\n{:<32} {:>10}\n"
        "{:<32} {:>10}\n"
        "{:<32} {:>10}\n"
        "{:<32} {:>10}\n"
        "{:<32} {:>10}\n",
        title + ":",
        st.count,
        "mispredicts direction:",
        st.mispredicted_direction,
        "mispredicts target:",
        st.mispredicted_target,
        "mispredicts total:",
        st.mispredicted,
        "btc corrections:",
        st.btc_clear);
}

void BobcatBPU::print_indirect_branch_stats(const std::string& title, const Statistics& st)
{
    fmt::print("\n-------------------------------------------\n{:<32} {:>10}\n", title + ":", st.count);

    if (st.count) {
        fmt::print(
            "{:<32} {:>10}\n"
            "{:<32} {:>10}\n"
            "{:<32} {:>10}\n"
            "{:<32} {:>10}\n"
            "{:<32} {:>10.2f}%\n"
            "{:<32} {:>10.2f}\n",
            "mispredicts direction:",
            st.mispredicted_direction,
            "mispredicts target:",
            st.mispredicted_target,
            "mispredicts total:",
            st.mispredicted,
            "btc corrections:",
            st.btc_clear,
            "misprediction rate:",
            (float)st.mispredicted_target / st.count * 100.0,
            "mpki:",
            (float)st.mispredicted_target / instructions * 1000.0);
    }
}

BobcatBPU::Statistics BobcatBPU::total_statistics() const
{
    Statistics totals;

    for (auto& s : stats) {
        totals.count += s.count;
        totals.taken += s.taken;
        totals.mispredicted += s.mispredicted;
        totals.mispredicted_direction += s.mispredicted_direction;
        totals.mispredicted_target += s.mispredicted_target;
        totals.btc_clear += s.btc_clear;
    }
    totals.btb_clear = btb_clears;

    return totals;
}

void BobcatBPU::print_stats(const std::string& trace)
{
    if (!instructions)
        return;

    Statistics totals = total_statistics();

    fmt::print(
        "\n-------------------------------------------\n"
        "Summary\n"
        "{} {:>36}\n"
        "{:<32} {:>10}\n"
        "{:<32} {:>10}\n"
        "{:<32} {:>10}\n"
        "{:<32} {:>10}\n"
        "{:<32} {:>10}\n"
        "{:<32} {:>10}\n"
        "{:<32} {:>10}\n"
        "{:<32} {:>10.2f}%\n"
        "{:<32} {:>10.2f}\n",
        "trace:",
        trace,
        "instructions:",
        instructions,
        "branches:",
        totals.count,
        "mispredicts direction:",
        totals.mispredicted_direction,
        "mispredicts target:",
        totals.mispredicted_target,
        "mispredicts total:",
        totals.mispredicted,
        "btc corrections:",
        totals.btc_clear,
        "btb corrections:",
        totals.btb_clear,
        "misprediction rate:",
        (float)totals.mispredicted_direction / totals.count * 100.0,
        "mpki:",
        (float)totals.mispredicted_direction / instructions * 1000.0);

    print_conditional_branch_stats("Conditional Direct", stats[core::BranchType::CONDITIONAL_DIRECT]);
    print_direct_branch_stats("Unconditional Direct", stats[core::BranchType::UNCONDITIONAL_DIRECT]);
    print_indirect_branch_stats("Unconditional Indirect", stats[core::BranchType::UNCONDITIONAL_INDIRECT]);
    print_direct_branch_stats("Direct Calls", stats[core::BranchType::CALL_DIRECT]);
    print_indirect_branch_stats("Indirect Calls", stats[core::BranchType::CALL_INDIRECT]);
    print_indirect_branch_stats("Returns", stats[core::BranchType::RETURN]);
}

void BobcatBPU::print_stats_csv(const std::string& trace, std::ostream& os)
{
    Statistics totals = total_statistics();

    os << "trace,instructions,branches,mispredicts,"
       << "conditional_branches,conditional_branches_taken,conditional_mispredicts,"
       << "unconditional_direct_branches,"
       << "unconditional_indirect_branches,unconditional_indirect_mispredicts,"
       << "call_direct_branches,"
       << "call_indirect_branches,call_indirect_mispredicts,"
       << "return_branches,return_mispredicts,btc_clears,btb_clears\n";

    os << trace << ',' << instructions << ',' << totals.count << ',' << totals.mispredicted << ','
       << stats[core::BranchType::CONDITIONAL_DIRECT].count << ',' << stats[core::BranchType::CONDITIONAL_DIRECT].taken
       << ',' << stats[core::BranchType::CONDITIONAL_DIRECT].mispredicted << ','
       << stats[core::BranchType::UNCONDITIONAL_DIRECT].count << ','
       << stats[core::BranchType::UNCONDITIONAL_INDIRECT].count << ','
       << stats[core::BranchType::UNCONDITIONAL_INDIRECT].mispredicted << ','
       << stats[core::BranchType::CALL_DIRECT].count << ',' << stats[core::BranchType::CALL_INDIRECT].count << ','
       << stats[core::BranchType::CALL_INDIRECT].mispredicted << ',' << stats[core::BranchType::RETURN].count << ','
       << stats[core::BranchType::RETURN].mispredicted << "," << totals.btc_clear << "," << totals.btb_clear
       << std::endl;
}

} // namespace ampere::bobcat

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
