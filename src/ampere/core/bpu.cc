//
// Copyright (c) 2018-2024 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <fmt/format.h>

#include "ampere/core/bpu.h"
#include "ampere/archx/branch_target.h"
#include "ampere/archx/pmu.h"

namespace ampere::core {

using namespace ampere::sc;
using namespace archx;
using namespace uarch_common;

static std::string line_prediction_as_string(const LinePrediction& line_prediction);

BPU::BPU(const sc_core::sc_module_name& name) : Module(name), tage("tage", bank_bits) {}

void BPU::init()
{
    update_pc(driver->next_pc());

    for (auto& hist : tage.get_conditional_histories())
        history.add_history_size(hist);

    /* FIXME: Doesn't work now because histories have to be added in order
    for (auto& hist : tage.get_indirect_histories())
        history.add_history_size(hist);
     */

    feq_throttled_size = feq.get_size();
}

void BPU::end_of_elaboration()
{
    token_generator = TokenGenerator(context_token_bits);
    context = BranchContext{driver->exception_level(), driver->asid(), token_generator.next(!use_context_token)};

    if (!restore_warmup_file.val().empty()) {
        fmt::print(std::cout, "Warmstart: BPU with file {}\n", restore_warmup_file.val());
        tage.restore_state(restore_warmup_file.val(), context);
    }
}

bool BPU::run()
{
    handle_feq_credit_returns();

    handle_loop_history();
    handle_loop_restart();

    bool gpc_flush = handle_flush();
    bool branch_check = handle_branch_check(gpc_flush);
    clearing = gpc_flush || branch_check;

    if (clearing) {
        if (perfect_nnp)
            prediction_perfect_nnp_pipe.clear();
        else if (ittage_delay == 0)
            prediction_pipe.clear();
        else
            prediction_late_ittage_pipe.clear();
        ursb[0].valid = false;
        ursb[1].valid = false;
    }
    if (!gpc_flush)
        handle_allocations();

    if (clearing || feq.get_count() >= feq_wake_threshold)
        sleep_state = SleepStates::AWAKE;

    fetch_stage_ran_this_cycle = false;
    nnp_stage_saw_indirect_this_cycle = false;

    if (perfect_nnp)
        prediction_perfect_nnp_pipe.run();
    else if (ittage_delay == 0)
        prediction_pipe.run();
    else
        prediction_late_ittage_pipe.run();

    handle_commits();

    handle_throttler();

    handle_loop_prediction_lockout();

    return true;
}

void BPU::handle_feq_credit_returns()
{
    while (feq_credit_return->can_take())
        feq_credit_return->take()->release();
}

void BPU::handle_throttler()
{
    // Handle BPU Throttling Logic
    //
    // The goal of frontend throttling is to reduce the maximum size of the
    // FEQ, and therefore avoid doing 'wasted' work, when the additional
    // decoupling of a larger FEQ is not providing a performance benefit.
    //
    // This is accomplished by keeping a counter, feq_throttle_counter, which
    // approximates the avoidable performance cost of frontend stalls. When the
    // estimated cost of avoidable frontend stalls exceeds a certain threshold,
    // we increase the FEQ size, when it goes below a threshold we decrease FEQ
    // size, and otherwise maintain it.
    //
    // The current 'cost' of each frontend stall is stored in
    // feq_throttle_fsd_value. "FSD" stands for Frontend Stall Decrease -
    // feq_throttle_counter is decreased by this amount every time we observe a
    // frontend stall which is not in the shadow of flush.
    // feq_throttle_fsd_value is set every epoch based on the number of GPC
    // flushes and IL1 misses observed during that epoch. In general, more
    // frequent IL1 misses increase the FSD value while more frequent GPC
    // flushes decrease it. The logic here is that the performance cost of IL1
    // misses can be avoided by hiding their latency with a deeper FEQ, while a
    // deeper FEQ results in more wasted work in the face of increased flushes.

    // First, handle updating the epoch-based counters. Count any icache misses
    // from the previous cycle (GPC flushes are counted in
    // BPU::handle_flush()). If the epoch has ended, set the next FSD (Frontend
    // Stall Decrease) value, then reset the values of the icache miss/flush
    // counters.
    if (icache_miss_port->can_take()) {
        feq_throttle_icache_misses += icache_miss_port->take();
    }
    if (instructions_committed_port->can_take()) {
        feq_throttle_epoch_instrs += instructions_committed_port->take();
    }
    if (feq_throttle_epoch_instrs >= feq_throttling_epoch_length) {
        if (feq_throttle_icache_misses >= feq_throttling_il1miss_cnt_hi) {
            if (feq_throttle_flushes >= feq_throttling_flush_cnt_hi) {
                feq_throttle_fsd_value = feq_throttling_fsd_0_0;
            } else if (feq_throttle_flushes >= feq_throttling_flush_cnt_lo) {
                feq_throttle_fsd_value = feq_throttling_fsd_0_1;
            } else {
                feq_throttle_fsd_value = feq_throttling_fsd_0_2;
            }
        } else if (feq_throttle_icache_misses >= feq_throttling_il1miss_cnt_lo) {
            if (feq_throttle_flushes >= feq_throttling_flush_cnt_hi) {
                feq_throttle_fsd_value = feq_throttling_fsd_1_0;
            } else if (feq_throttle_flushes >= feq_throttling_flush_cnt_lo) {
                feq_throttle_fsd_value = feq_throttling_fsd_1_1;
            } else {
                feq_throttle_fsd_value = feq_throttling_fsd_1_2;
            }
        } else {
            if (feq_throttle_flushes >= feq_throttling_flush_cnt_hi) {
                feq_throttle_fsd_value = feq_throttling_fsd_2_0;
            } else if (feq_throttle_flushes >= feq_throttling_flush_cnt_lo) {
                feq_throttle_fsd_value = feq_throttling_fsd_2_1;
            } else {
                feq_throttle_fsd_value = feq_throttling_fsd_2_2;
            }
        }

        feq_throttle_epoch_instrs = 0;
        feq_throttle_flushes = 0;
        feq_throttle_icache_misses = 0;
    }

    if (feq_throttle_flush_shadow && feq.get_occupancy() >= feq_throttled_size)
        feq_throttle_flush_shadow = false;

    // Decrement the throttling counter by `feq_throttle_fsd_value` for
    // every frontend stall we observe, unless in the shadow of a flush
    if (frontend_stall->can_take()) {
        unsigned frontend_stalls = frontend_stall->take();
        if (!feq_throttle_flush_shadow) {
            feq_throttle_counter -= (frontend_stalls * feq_throttle_fsd_value);
        }
    }

    // Increase feq_throttle_counter once per cycle
    feq_throttle_counter++;

    // Finally, increase or decrease the max (throttled) FEQ size if the
    // counter has overflowed the positive or negative thresholds

    if (feq_throttle_counter >= (int64_t)feq_throttling_cred_threshold) {
        if (feq_throttled_size > feq_throttling_min_feq_size) {
            feq_throttled_size =
                std::max(feq_throttled_size - feq_throttling_granularity, feq_throttling_min_feq_size.val());
            pmu_event(pmu_num_data(archx::PMU::ICF_FEQ_SIZE_DEC));
        }
        feq_throttle_counter = 0;
    } else if (feq_throttle_counter < -(int64_t)feq_throttling_cred_threshold) {
        if (feq_throttled_size < feq.get_size()) {
            feq_throttled_size = std::min(feq_throttled_size + feq_throttling_granularity, feq.get_size());
            pmu_event(pmu_num_data(archx::PMU::ICF_FEQ_SIZE_INC));
        }
        feq_throttle_counter = 0;
    }

    // The i += 4; encoding for the current FEQ size (count every cycle by the current
    // size of the FEQ), is:
    //   0 -> 12 entries (minimum size)
    //   1 -> 16 entries
    //   2 -> 20 entries
    //   3 -> 24 entries
    //   4 -> 28 entries
    //   5 -> 32 entries (maximum size)
    if (enable_per_cycle_pmu && feq_throttled_size >= 16) {
        for (size_t i = 16; i <= feq_throttled_size; i += 4)
            pmu_event(pmu_num_data(archx::PMU::ICF_FEQ_SIZE_PERCYC));
    }
}

void BPU::handle_loop_prediction_lockout()
{
    Cycle current_cycle = get_cycle();
    if (loop_prediction_lockout_end && current_cycle >= loop_prediction_lockout_end) {
        // If we were previously locked out, but have fallen off the end of
        // that lockout time period, re-enable the loop predictor, and setup
        // for the next epoch.
        ld.end_prediction_lockout();
        loop_prediction_lockout_end = 0;
        loop_prediction_lockout_epoch_end = current_cycle + loop_prediction_lockout_epoch;
        ld.end_lockout_epoch();
    } else if (!loop_prediction_lockout_end && loop_prediction_lockout_hit_threshold > 0
               && ld.lockout_epoch_prediction_hits() >= loop_prediction_lockout_hit_threshold)
    {
        // If we have seen more hits than our threshold in the current epoch,
        // begin a lockout
        loop_prediction_lockout_end = current_cycle + loop_prediction_lockout_length;
        ld.begin_prediction_lockout();
    } else if (current_cycle >= loop_prediction_lockout_epoch_end) {
        // If we have seen an epoch's worth of cycles, begin a new one
        loop_prediction_lockout_epoch_end = current_cycle + loop_prediction_lockout_epoch;
        ld.end_lockout_epoch();
    }
}

bool BPU::feq_can_acquire()
{
    return feq.can_acquire() && (feq.get_count() > feq_sleep_threshold)
           && (!feq_throttling_enabled || feq.get_occupancy() < feq_throttled_size);
}

bool BPU::fetch_stage(Pipeline<Op>::Data data)
{
    // Don't run the BPU if we are a stashing core
    if (stash_only)
        return false;

    // Only run the fetch stage once per cycle (even if we have a double-wide
    // pipeline), because there is only one 'next' address for us to read, we
    // only need to count the PMUs once this cycle, etc.
    if (fetch_stage_ran_this_cycle) {
        // But, inject a garbage data item if the "real" fetch didn't stall so
        // it can still flow to nnp_stage (it will be squashed there if we
        // can't double-predict this cycle)
        if (last_fetch_cycle == get_cycle())
            data = make<Op>();
        return true;
    }
    fetch_stage_ran_this_cycle = true;

    auto current_pc = consume_pc();

    do_update();

    bool block_for_update = update_stalls_predict && (update_state == 1 || update_state == 3);
    bool bhr_full_stall = history.uncommitted_depth() > max_uncommitted_takens;

    if (!stalling_fetch_address && (current_pc == NON_CANONICAL_TARGET || current_pc.extract_range(1, 0) != 0)) {
        stalling_fetch_address = current_pc;
    }

    if (!feq_can_acquire())
        pmu_event(pmu_num_data(archx::PMU::ICF_FEQ_FULL));

    if (ld.looping()) {
        ev_looping();
        // LOOP_MODE_CYCLE_CNTR is computed in rollup_formula.py as equal to ev_looping
    } else if (feq_sleep_threshold && sleep_state == SleepStates::ASLEEP) {
        asleep();
    } else if (bhr_full_stall) {
        ev_bhr_full_stall();
        pmu_event(pmu_num_data(archx::PMU::BPU_BHR_FULL_STALL));
    } else if (tage.rsb_is_full()) {
        pmu_event(pmu_num_data(archx::PMU::BPU_RSB_FULL_STALL));
    } else if (stalling_fetch_address) {
        ev_address_stall(entrance_pc(stalling_fetch_address.value()));
    } else if (clear->can_put() && prediction->can_put() && !block_for_update) {
        if (feq_can_acquire()) {
            Fetch fetch;

            // In perfect_el0 mode, we may enter from EL0 through a mispredicted path on EL1 - do not predict_perfectly
            // in these cases
            bool do_perfect = perfect || (perfect_el0 && (context.el == archx::EL::EL0) && !mispredicted_el0_path);

            if (do_perfect) {
                fetch = predict_perfectly(current_pc);
                update_pc(fetch->next_program_counter());
            } else {
                fetch = make<Fetch>(current_pc, current_fetch_id++);
            }
            // Reset the early predictor to use next to the NNP at the
            // beginning of each cycle (unless we are waiting on the first
            // post-flush prediction)
            if (next_early_predictor != EarlyPredictor::NP)
                next_early_predictor = EarlyPredictor::NNP;
            fetch->bank = determine_bank(fetch->program_counter);
            send_fetch(fetch);

            pc_mux_event(fetch,
                         fetch_id_data(fetch->fetch_id),
                         fetch->program_counter,
                         br_target(fetch->next_program_counter()),
                         idle_cycles_data(get_cycle() - last_fetch_cycle));

            if (!do_perfect)
                data = make_clone<Op>(fetch, fetch);
            last_fetch_cycle = get_cycle();
        } else {
            sleep_state = SleepStates::ASLEEP;
        }
    }

    return true;
}

bool BPU::nnp_stage(Pipeline<Op>::Data data)
{
    if (clear->can_put() && !tage.rsb_is_full()) {
        // Do a double-prediction if we can this cycle
        if (!data.get()->initialized) {
            BPUBank next_bank = determine_bank(pc);
            if ((!one_indirect_per_cycle || !nnp_stage_saw_indirect_this_cycle)
                && (next_early_predictor == EarlyPredictor::NNPP || next_early_predictor == EarlyPredictor::NNNP)
                && (ignore_bank_conflict || next_bank != prev_bank))
            {
                Fetch fetch;
                auto current_pc = consume_pc();
                // In perfect_el0 mode, we may enter from EL0 through a mispredicted path on EL1 - do not
                // predict_perfectly in these cases
                bool do_perfect = perfect || (perfect_el0 && (context.el == archx::EL::EL0) && !mispredicted_el0_path);
                if (do_perfect) {
                    fetch = predict_perfectly(current_pc);
                    update_pc(fetch->next_program_counter());
                } else {
                    fetch = make<Fetch>(current_pc, current_fetch_id++);
                }
                fetch->bank = next_bank;
                send_fetch(fetch);
                if (do_perfect) {
                    data.invalidate();
                    return true;
                } else {
                    data = make_clone<Op>(fetch, fetch);
                }
            } else {
                data.invalidate();
                return true;
            }
        }

        Fetch fetch = data.get()->fetch;

        boost::optional<NNPEntry&> hit;
        if (fetch->early_predictor == EarlyPredictor::NP)
            hit = np.lookup(fetch);
        else if (fetch->early_predictor == EarlyPredictor::NNPP)
            hit = nnpp.lookup(fetch);
        else if (fetch->early_predictor == EarlyPredictor::NNP)
            hit = nnp.lookup(fetch);
        else if (fetch->early_predictor == EarlyPredictor::NNNP)
            hit = nnnp.lookup(fetch);
        else
            AMPERE_FATAL("Unexpected EarlyPredictor value in fetch->early_predictor");

        if (hit) {
            fetch->update_prediction(
                {hit->branch_type, fetch->cache_line() + (hit->offset * 4UL), hit->target, hit->target_nextline});

            if (ursb_enabled)
                lookup_ursb(fetch, *hit);

            if (is_indirect(fetch->get_prediction().branch_type))
                nnp_stage_saw_indirect_this_cycle = true;

            if (double_predict_known_nextline && fetch->get_prediction().target_nextline && feq_can_acquire()) {
                // If the target of the current prediction is guaranteed to go
                // nextline, create a new fetch
                ProgramCounter target = fetch->get_prediction().target;
                auto next_fetch = make<Fetch>(target, current_fetch_id++);

                // because this fetch doesn't lookup prediction structures on
                // its own, it shares the bank of its spawning prediction
                next_fetch->bank = prev_bank;

                send_fetch(next_fetch);
                update_pc(target.next_line_address());
                data.get()->target_nextline_fetch = next_fetch;

                double_nextline_predict(next_fetch,
                                        fetch_id_data(next_fetch->fetch_id),
                                        next_fetch->program_counter,
                                        br_type(next_fetch->get_prediction().branch_type),
                                        br_target(next_fetch->get_prediction().target),
                                        br_taken(next_fetch->get_prediction().is_taken()));
            } else {
                update_pc(fetch->next_program_counter());
            }
        } else {
            update_pc(pc.next_line_address());
        }

        if (fetch->early_predictor == EarlyPredictor::NP) {
            np_predict(fetch,
                       fetch_id_data(fetch->fetch_id),
                       fetch->program_counter,
                       br_type(fetch->get_prediction().branch_type),
                       br_pc(fetch->get_prediction().branch_pc),
                       br_target(fetch->get_prediction().target),
                       br_taken(fetch->get_prediction().is_taken()),
                       br_prev_pc(fetch->prev_program_counter_uncorrected));
        } else if (fetch->early_predictor == EarlyPredictor::NNPP) {
            nnpp_predict(fetch,
                         fetch_id_data(fetch->fetch_id),
                         fetch->program_counter,
                         br_type(fetch->get_prediction().branch_type),
                         br_pc(fetch->get_prediction().branch_pc),
                         br_target(fetch->get_prediction().target),
                         br_taken(fetch->get_prediction().is_taken()),
                         br_prev_pc(fetch->prev_program_counter_uncorrected));
        } else if (fetch->early_predictor == EarlyPredictor::NNP) {
            nnp_predict(fetch,
                        fetch_id_data(fetch->fetch_id),
                        fetch->program_counter,
                        br_type(fetch->get_prediction().branch_type),
                        br_pc(fetch->get_prediction().branch_pc),
                        br_target(fetch->get_prediction().target),
                        br_taken(fetch->get_prediction().is_taken()),
                        br_prev_pc(fetch->prev_program_counter_uncorrected));
        } else if (fetch->early_predictor == EarlyPredictor::NNNP) {
            nnnp_predict(fetch,
                         fetch_id_data(fetch->fetch_id),
                         fetch->program_counter,
                         br_type(fetch->get_prediction().branch_type),
                         br_pc(fetch->get_prediction().branch_pc),
                         br_target(fetch->get_prediction().target),
                         br_taken(fetch->get_prediction().is_taken()),
                         br_prev_pc(fetch->prev_program_counter_uncorrected));
        } else {
            AMPERE_FATAL("Unexpected EarlyPredictor value in fetch->early_predictor");
        }
    } else {
        ursb[0].valid = false;
        ursb[1].valid = false;
        data.invalidate();
    }

    return true;
}

void BPU::lookup_ursb(Fetch& fetch, const NNPEntry& nnp_entry)
{
    if (is_return(fetch->get_prediction().branch_type) && ursb[ursb_tos].valid) {
        fetch->update_prediction_target(ursb[ursb_tos].target);
        ursb[ursb_tos].valid = false;
        ursb_tos ^= 1;
    } else if (is_call(fetch->get_prediction().branch_type)) {
        ursb_tos ^= 1;
        ursb[ursb_tos].valid = true;
        ursb[ursb_tos].target = fetch->get_prediction().branch_pc + 4;
    }
}

/*
 * Called when we are BTB-clearing because the BTB/TAGE/ITTAGE prediction
 * disagreed with that from the BTB. If we are able to insert a known-nextline
 * prediction at the target of the clear, do so and return false. Otherwise,
 * return false so the clear can be handled normally without an injected
 * prediction.
 */
bool BPU::nextline_predict_on_clear(Pipeline<Op>::Data data, Fetch& fetch)
{
    if (!double_predict_known_nextline || !fetch->get_prediction().target_nextline || !feq_can_acquire()) {
        return false;
    }

    // If the target of the new prediction is guaranteed to go
    // nextline, inject a new fetch and clear to the following line
    ProgramCounter target = fetch->get_prediction().target;
    auto next_fetch = make<Fetch>(target, current_fetch_id++);

    // because this fetch doesn't lookup prediction structures on its own, it
    // shares the bank of its spawning prediction
    next_fetch->bank = prev_bank;

    send_fetch(next_fetch);
    data.get()->target_nextline_fetch = next_fetch;
    fetch->post_clear_nextline_target_fetch_id = next_fetch->fetch_id;
    update_pc(target.next_line_address(), true /*force*/);
    ld.predict(next_fetch, history, context);

    double_nextline_predict(next_fetch,
                            fetch_id_data(next_fetch->fetch_id),
                            next_fetch->program_counter,
                            br_type(next_fetch->get_prediction().branch_type),
                            br_target(next_fetch->get_prediction().target),
                            br_taken(next_fetch->get_prediction().is_taken()));

    return true;
}

bool BPU::btb_stage(Pipeline<Op>::Data data)
{
    Op op = data.get();
    Fetch fetch = op->fetch;

    Prediction old_prediction = fetch->get_prediction();
    tage.predict(fetch, history, context);

    if (ld_enabled) {
        ld.predict(fetch, history, context);
        if (ld.looping()) {
            fetch->clear_type = ClearType::LOOP_START;
            clear->put(fetch);
        }
    }

    // RSB becomes full if the instruction is a call. In this case, a BTB clear
    // of LOOP_START type won't happen as the call instruction results in a
    // loop abort. As a result, clear->can_put() check is not required here.
    if (tage.rsb_is_full()) {
        fetch->clear_type = ClearType::RSB_FULL;
        clear->put(fetch);
        ld.clear(fetch, false);
        update_pc(fetch->next_program_counter());
        prev_prev_pc = fetch->prev_program_counter;
        prev_prev_pc_uncorrected = fetch->prev_program_counter_uncorrected;
        prev_pc = fetch->program_counter;
        prev_pc_uncorrected = fetch->program_counter;
        prev_bank = fetch->bank;
        reuse_bank = true;
    }

    btb_predict(fetch,
                fetch_id_data(fetch->fetch_id),
                fetch->program_counter,
                br_type(fetch->get_prediction().branch_type),
                br_pc(fetch->get_prediction().branch_pc),
                br_target(fetch->get_prediction().target),
                br_target_nextline(fetch->get_prediction().target_nextline),
                br_taken(fetch->get_prediction().is_taken()),
                br_line_prediction(line_prediction_as_string(fetch->line_prediction)));

    if (perfect_nnp) {
        update_pc(fetch->next_program_counter());
        prev_prev_pc = fetch->prev_program_counter;
        prev_prev_pc_uncorrected = fetch->prev_program_counter_uncorrected;
        prev_pc = fetch->program_counter;
        prev_pc_uncorrected = fetch->program_counter;
        prev_bank = fetch->bank;
    } else {
        if (prediction_changed(old_prediction, fetch->get_prediction())) {
            // Skip if another BTB clear happened in the same cycle
            // due to a RSB full or loop detector looping.
            bool oldest_clear = false;
            if (clear->can_put()) {
                fetch->clear_type = clear_type(old_prediction, fetch->get_prediction());
                clear->put(fetch);
                ld.clear(fetch, false);
                tage.flush_rsb(fetch);
                oldest_clear = true;
            }

            prev_prev_pc = fetch->prev_program_counter;
            prev_prev_pc_uncorrected = fetch->prev_program_counter_uncorrected;
            prev_pc = fetch->program_counter;
            prev_pc_uncorrected = fetch->program_counter;

            prev_bank = fetch->bank;
            // Reuse the same bank if the newly-updated prediction is not
            // taken, and the next PC is 4 later than our exit PC, and also not
            // crossing a cacheline
            reuse_bank = !fetch->get_prediction().is_taken() && fetch->next_program_counter() == (fetch->exit_pc() + 4)
                         && fetch->next_program_counter().line_offset() != 0;

            // If the target of the new prediction is guaranteed to go
            // nextline, inject a new fetch and clear to the following line.
            if (!oldest_clear || !nextline_predict_on_clear(data, fetch)) {
                // But, if we can't inject a nextline prediction upon clear,
                // clear as normal without injecting an extra prediction.
                data.get()->target_nextline_fetch = std::nullopt;
                update_pc(fetch->next_program_counter(), true /* force update */);
            }

            update_nnp(fetch, false /*useful*/);
            nnp_train(fetch,
                      fetch->program_counter,
                      br_target(fetch->next_program_counter()),
                      br_target_nextline(fetch->get_prediction().target_nextline),
                      br_early_predictor(fetch->early_predictor));
        } else {
            // If we are carrying along a 'target nextline' fetch, the loop
            // detector/predictor still wants to know about it to get its
            // instruction counting correct.
            if (!ld.looping() && data.get()->target_nextline_fetch) {
                ld.predict(*data.get()->target_nextline_fetch, history, context);
            }

            update_nnp(fetch, true /*useful*/);
        }
    }

    update_history(fetch);
    return true;
}

bool BPU::btb_tage_only_stage(Pipeline<Op>::Data data)
{
    if (!clear->can_put()) {
        data.invalidate();
        return true; // later stage is already clearing
    }

    Op op = data.get();
    Fetch fetch = op->fetch;

    Prediction old_prediction = fetch->get_prediction();
    tage.predict_tage(fetch, history, context);

    if (ld_enabled) {
        ld.predict(fetch, history, context);
        if (ld.looping()) {
            fetch->clear_type = ClearType::LOOP_START;
            clear->put(fetch);
        }
    }

    // RSB becomes full if the instruction is a call. In this case, a BTB clear
    // of LOOP_START type won't happen as the call instruction results in a
    // loop abort. As a result, clear->can_put() check is not required here.
    if (tage.rsb_is_full()) {
        fetch->clear_type = ClearType::RSB_FULL;
        clear->put(fetch);
        ld.clear(fetch, false);
        update_pc(fetch->next_program_counter());
        prev_prev_pc = fetch->prev_program_counter;
        prev_prev_pc_uncorrected = fetch->prev_program_counter_uncorrected;
        prev_pc = fetch->program_counter;
        prev_pc_uncorrected = fetch->program_counter;
        prev_bank = fetch->bank;
        reuse_bank = true;
    }

    op->tage_changed_prediction = prediction_changed(old_prediction, fetch->get_prediction());

    update_history(fetch);

    if (op->tage_changed_prediction) {
        // Skip if another BTB clear happened in the same cycle
        // due to a RSB full or loop detector looping.
        bool oldest_clear = false;
        if (clear->can_put()) {
            fetch->clear_type = clear_type(old_prediction, fetch->get_prediction());
            clear->put(fetch);
            ld.clear(fetch, false);
            tage.flush_rsb(fetch);
            oldest_clear = true;
        }

        prev_prev_pc = fetch->prev_program_counter;
        prev_prev_pc_uncorrected = fetch->prev_program_counter_uncorrected;
        prev_pc = fetch->program_counter;
        prev_pc_uncorrected = fetch->program_counter;

        prev_bank = fetch->bank;
        // Reuse the same bank if the newly-updated prediction is not
        // taken, and the next PC is 4 later than our exit PC, and also not
        // crossing a cacheline
        reuse_bank = !fetch->get_prediction().is_taken() && fetch->next_program_counter() == (fetch->exit_pc() + 4)
                     && fetch->next_program_counter().line_offset() != 0;

        // If the target of the new prediction is guaranteed to go
        // nextline, inject a new fetch and clear to the following line.
        if (!oldest_clear || !nextline_predict_on_clear(data, fetch)) {
            // But, if we can't inject a nextline prediction upon clear, clear
            // as normal without injecting an extra prediction.
            data.get()->target_nextline_fetch = std::nullopt;
            update_pc(fetch->next_program_counter(), true /*force*/);
        }
    } else if (!ld.looping() && data.get()->target_nextline_fetch) {
        // If we are carrying along a 'target nextline' fetch, the loop
        // detector/predictor still wants to know about it to get its
        // instruction counting correct.
        ld.predict(*data.get()->target_nextline_fetch, history, context);
    }

    btb_predict(fetch,
                fetch_id_data(fetch->fetch_id),
                fetch->program_counter,
                br_type(fetch->get_prediction().branch_type),
                br_pc(fetch->get_prediction().branch_pc),
                br_target(fetch->get_prediction().target),
                br_target_nextline(fetch->get_prediction().target_nextline),
                br_taken(fetch->get_prediction().is_taken()),
                br_line_prediction(line_prediction_as_string(fetch->line_prediction)));

    bool has_any_indirect = false;

    for (size_t offset = fetch->program_counter.extract_range(5, 2); offset < fetch->line_prediction.size(); ++offset) {
        auto branch_type = fetch->line_prediction[offset].branch_type;
        if (is_indirect(branch_type) && !is_return(branch_type)) {
            has_any_indirect = true;
            break;
        }
    }

    bool is_indirect_not_ret =
        (is_indirect(fetch->get_prediction().branch_type) && !is_return(fetch->get_prediction().branch_type));
    bool icf_not_started_looping = !ld.looping() || (fetch->loop_collect && !fetch->loop_end);

    if (is_indirect_not_ret && icf_not_started_looping) {
        op->ittage_active = true;

        // If we can't put, it can only be because we're already stalling with an
        // older fetch, and that's okay.
        if (ittage_stall->can_put())
            ittage_stall->put(fetch->fetch_id);
    } else if (stall_on_any_indirect && has_any_indirect && ittage_stall->can_put()) {
        ittage_stall->put(fetch->fetch_id);
    }

    return true;
}

bool BPU::ittage_stall_stage(Pipeline<Op>::Data data)
{
    if (!clear->can_put()) {
        data.invalidate();
        return true; // later stage is already clearing
    }

    Op op = data.get();
    Fetch fetch = op->fetch;

    if (op->ittage_active && ittage_stall->can_put())
        ittage_stall->put(fetch->fetch_id);

    return true;
}

bool BPU::ittage_clear_stage(Pipeline<Op>::Data data)
{
    Op op = data.get();
    Fetch fetch = op->fetch;

    bool ittage_changed_prediction = false;

    if (op->ittage_active) {
        Prediction old_prediction = fetch->get_prediction();
        tage.predict_ittage(fetch, history, context);

        ittage_changed_prediction = prediction_changed(old_prediction, fetch->get_prediction());

        if (ittage_changed_prediction) {
            bool icf_not_started_looping = !ld.looping() || (fetch->loop_collect && !fetch->loop_end);
            bool oldest_clear = false;
            if (!ld_enabled || icf_not_started_looping) {
                fetch->clear_type = ClearType::DIFFERENT_INDIRECT_TARGET;
                clear->put(fetch);
                ld.clear(fetch, true);
                tage.flush_rsb(fetch);
                oldest_clear = true;
            }

            prev_prev_pc = fetch->prev_program_counter;
            prev_prev_pc_uncorrected = fetch->prev_program_counter_uncorrected;
            prev_pc = fetch->program_counter;
            prev_pc_uncorrected = fetch->program_counter;
            prev_bank = fetch->bank;

            history.flush(fetch->fetch_id);
            update_history(fetch);

            // If the target of the new prediction is guaranteed to go
            // nextline, inject a new fetch and clear to the following line.
            if (!oldest_clear || !nextline_predict_on_clear(data, fetch)) {
                // If we can't inject a nextline prediction upon clear, clear
                // as normal without injecting an extra prediction.
                data.get()->target_nextline_fetch = std::nullopt;
                update_pc(fetch->next_program_counter(), true /*force*/);
            }
        }

        ittage_predict(fetch,
                       fetch_id_data(fetch->fetch_id),
                       fetch->program_counter,
                       br_type(fetch->get_prediction().branch_type),
                       br_pc(fetch->get_prediction().branch_pc),
                       br_target(fetch->get_prediction().target),
                       br_target_nextline(fetch->get_prediction().target_nextline),
                       br_taken(fetch->get_prediction().is_taken()),
                       br_line_prediction(line_prediction_as_string(fetch->line_prediction)));
    }

    if (ittage_changed_prediction || op->tage_changed_prediction) {
        update_nnp(fetch, false /*useful*/);
        nnp_train(fetch,
                  fetch->program_counter,
                  br_target(fetch->next_program_counter()),
                  br_target_nextline(fetch->get_prediction().target_nextline));
    } else {
        update_nnp(fetch, true /*useful*/);
    }

    return true;
}

/*
 * Update the branch context, returning whether the non-EL context was updated.
 * In other words return false iff no context was changed, or only EL was
 * changed. This would ideally also comprehend VMID, TR, NS, SCXTNUM_ELx.
 */
bool BPU::update_context()
{
    archx::EL el = driver->exception_level();
    uint16_t asid = driver->asid();
    bool context_change = (asid != context.asid);

    // RSB pops (TOS updates) even if the EL check fails. This results in mis-alignment
    // of calls/rets until a flush (or re-steer in cases of mispredicted EL updates).
    // To prevent this occuring frequently in perfect_el0 mode, flush the RSB on an
    // EL update.
    if ((perfect_el0) && (el != context.el))
        tage.clear_rsb();

    if (context_change)
        context = BranchContext{el, asid, token_generator.next(!use_context_token)};
    else if (el != context.el)
        context = BranchContext{el, asid, context.token};

    return context_change;
}

void BPU::update_history(Fetch& fetch)
{
    // We only pass the branch target PC for indirect branches. All other
    // branches only hash in the branch PC.
    if (is_conditional(fetch->get_prediction().branch_type) && fetch->get_prediction().is_taken())
        history.push(fetch->fetch_id, fetch->get_prediction().branch_pc);
    else if (is_indirect(fetch->get_prediction().branch_type))
        history.push(fetch->fetch_id, fetch->get_prediction().branch_pc, fetch->get_prediction().target);
    else if (is_unconditional(fetch->get_prediction().branch_type))
        history.push(fetch->fetch_id, fetch->get_prediction().branch_pc);
}

void BPU::update_pc(ProgramCounter new_pc, bool force)
{
    if (!pc_is_valid || force) {
        pc = new_pc;
        pc_is_valid = true;
    }
}

ProgramCounter BPU::consume_pc()
{
    pc_is_valid = false;
    return pc;
}

Fetch BPU::predict_perfectly(ProgramCounter current_pc)
{
    ProgramCounter next_pc = current_pc;
    ProgramCounter last_pc;
    bool taken_branch;

    do {
        last_pc = next_pc;
        next_pc = driver->next_pc(&taken_branch);
    } while (current_pc.same_cache_line(next_pc) && !taken_branch);

    auto fetch = make<Fetch>(current_pc, current_fetch_id++);

    if (taken_branch)
        fetch->update_prediction({BranchType::UNSPECIFIED, last_pc, next_pc});

    return fetch;
}

/*
 * Determine the BPU bank based on the previous bank (reusing it if indicated),
 * and the current banking scheme.
 */
BPUBank BPU::determine_bank(ProgramCounter prediction_entry) const
{
    if (!bank_bits)
        return 0;
    else if (reuse_banks_on_same_consistent_prediction && reuse_bank)
        return prev_bank;

    // First, calculate the bank based on the prediction PC alone
    BPUBank desired_bank =
        fold_to_n_bits(prediction_entry.extract_range(bank_start_bit + bank_input_bits - 1, bank_start_bit), bank_bits);

    // If applicable, modify the bank using any bank conflict avoidance schemes
    // in place
    if (increment_on_bank_conflict && desired_bank == prev_bank)
        return (desired_bank + 1) & ((1UL << bank_bits) - 1);
    else
        return desired_bank;
}

void BPU::send_fetch(Fetch& fetch)
{
    fetch->prev_prev_program_counter = prev_prev_pc;
    fetch->prev_prev_program_counter_uncorrected = prev_prev_pc_uncorrected;
    fetch->prev_program_counter = prev_pc;
    fetch->prev_program_counter_uncorrected = prev_pc_uncorrected;
    prev_prev_pc = prev_pc;
    prev_prev_pc_uncorrected = prev_pc_uncorrected;
    prev_pc = fetch->program_counter;
    prev_pc_uncorrected = fetch->program_counter;

    reuse_bank = false;
    prev_bank = fetch->bank;

    fetch->early_predictor = next_early_predictor;

    if (next_early_predictor == EarlyPredictor::NP)
        next_early_predictor = EarlyPredictor::NNPP;
    else if (next_early_predictor == EarlyPredictor::NNP)
        next_early_predictor = EarlyPredictor::NNNP;

    fetch->feq_credit = feq.acquire();
    prediction->put(fetch);
}

void BPU::update_nnp(Fetch& fetch, bool useful)
{
    if (fetch->early_predictor == EarlyPredictor::NP)
        np.update(fetch, useful);
    else if (fetch->early_predictor == EarlyPredictor::NNPP)
        nnpp.update(fetch, useful);
    else if (fetch->early_predictor == EarlyPredictor::NNP)
        nnp.update(fetch, useful);
    else if (fetch->early_predictor == EarlyPredictor::NNNP)
        nnnp.update(fetch, useful);
    else
        AMPERE_FATAL("Unexpected EarlyPredictor value in fetch->early_predictor");
}

void BPU::handle_commits()
{
    if (branches_committed->can_take())
        pending_commits += branches_committed->take();

    assert(pending_commits <= bob.size());
}

int BPU::get_update_state(BOBEntry& bob_entry) const
{
    const BranchInfo& br_info = bob_entry.br_info;
    bool mispredicted = br_info.mispredicted_target || br_info.mispredicted_direction;

    if (always_update) {
        return 1;
    } else if (mispredicted || (br_info.btb_miss && br_info.taken)) {
        if (ittage_rmw_delay && (is_indirect(br_info.type) && !is_return(br_info.type)) && !br_info.btb_miss)
            return ittage_rmw_delay;
        else
            return 3;
    } else if ((is_conditional(br_info.type) || (is_indirect(br_info.type) && !is_return(br_info.type)))
               && (tage.primary_needs_update(br_info) || tage.alternate_needs_update(br_info)))
    {
        if (ittage_write_delay && (is_indirect(br_info.type) && !is_return(br_info.type)) && br_info.ittage_hit)
            return ittage_write_delay;
        else
            return 1;
    }

    return 0;
}

void BPU::do_update()
{
    if (update_state && --update_state > 0)
        return;

    int commits_this_cycle = 0;
    bool committed_update{false};
    uint required_pending_commits = (double_predict_known_nextline || require_2_pending_commits) ? 1 : 0;

    auto first_uncommitted =
        std::find_if(bob.begin(), bob.end(), [](auto& entry) { return !(entry.second.committed); });
    while (pending_commits > required_pending_commits && commits_this_cycle < commits_per_cycle) {
        auto& bob_entry = first_uncommitted->second;
        AMPERE_ASSERT(!bob_entry.committed, "Attempting to do branch update to already-committed BOB entry!");

        if (double_predict_known_nextline && bob_entry.taken()) {
            auto second_uncommitted = next(first_uncommitted);
            auto& next_bob_entry = second_uncommitted->second;
            auto next_br_pc = next_bob_entry.pc();
            // TODO ensure a context switch hasn't happened between when we
            // took our target and this branch. How? Should we save the current
            // BranchContext to each Fetch and ensure it is the same here?
            // Should we "color" fetches (and therefore their associated BOB
            // entries) every time we see a context-synchronizing flush?
            bob_entry.br_info.target_nextline = bob_entry.br_info.target.line_address() != next_br_pc.line_address();
        }

        // Even if we can commit more than one branch per cycle, we can only
        // handle one 'real' update, meaning a BTB/Bi-modal/TAGE/ITTAGE update
        // or any taken branch. If this branch was fetched while in loop mode,
        // though, we can always merge it because it will never update.
        bool mergeable_update =
            allow_merging_all_updates || bob_entry.looped()
            || (!bob_entry.taken() && !get_update_state(bob_entry) && !tage.bimodal_needs_update(bob_entry.br_info));
        if (!mergeable_update && committed_update)
            break;

        auto fetch_id = bob_entry.fetch_id();

        bob_entry.br_info.instrs_since_last_branch = bob_entry.instrs_since_last_branch();

        if (train(bob_entry)) {
            if (bob_entry.taken() && (!bob_entry.looped() || update_history_while_looping))
                history.commit(fetch_id);

            ++commits_this_cycle;
            --pending_commits;
            if (!mergeable_update)
                committed_update = true;

            bob_entry.committed = true;
            first_uncommitted++;
        } else {
            break;
        }
    }

    // Release all but the last committed BOB ID. RTL cannot ever allocate the
    // last BOB ID because other structures may reference the immediately-older
    // BOB ID (but that older BOB ID will commit and deallocate first, and
    // therefore cannot be re-allocated without risking overwriting it)
    size_t committed_bobids = std::count_if(bob.begin(), bob.end(), [](auto& entry) { return entry.second.committed; });
    while (committed_bobids > 1) {
        bob_credit_return->put(bob.begin()->second.extract_bobid());
        bob.erase(bob.begin());
        committed_bobids--;
    }
}

bool BPU::train(BOBEntry& bob_entry)
{
    const BranchInfo& br_info = bob_entry.br_info;
    const BranchContext& br_context = bob_entry.br_context;

    if (perfect || (perfect_el0 && (br_context.el == archx::EL::EL0)))
        return true;

    if (((br_info.btb_miss && is_indirect(br_info.type) && !is_return(br_info.type)))
        || (!br_info.rsb_hit && is_return(br_info.type)))
        pmu_event(pmu_num_data(archx::PMU::BPU_IND_NO_PREDICT_RETIRED));

    if (bob_entry.looped()) {
        // Don't train TAGE on uops fetched directly from the IQ while in loop
        // mode, but we want to still notify the loop detector about branches
        // which are occurring
        ld.train(br_info, history, br_context);
        return true;
    }

    auto new_update_state = get_update_state(bob_entry);
    if (allow_merging_all_updates) {
        update_state = std::max(update_state, new_update_state);
    } else {
        AMPERE_ASSERT(update_state == 0 || new_update_state == 0, "Tried to do two overlapping non-empty updates");
        update_state = new_update_state;
    }

    bool any_clear = clearing || !clear->can_put();

    if (update_state) {
        if (any_clear) {
            // Can't update in the same cycle as any kind of clear, unless it's just BTB LRU
            update_state = 0;
            return false;
        }
        tage.train(br_info, history, br_context);
    } else {
        if (!tage.train_bimodal_only(br_info)) {
            // train_bimodal_only returns false if the training is not just a strengthening
            // (that is, it returns false if the bimodal was incorrect). We can't flip the
            // direction of the bimodal without a "real" update, and we can't do a real update
            // in the same cycle as any clear, so we have to give up and try later.
            //
            if (any_clear) {
                update_state = 0;
                return false;
            }
        }
        tage.update_btb_lru(br_info, br_context);
        tage.update_rsb(br_info, history, br_context);
    }

    bool mispredicted = br_info.mispredicted_target || br_info.mispredicted_direction;

    if (enable_pipeline_mispredict_event && mispredicted && is_conditional(br_info.type)) {
        auto fetch = br_info.fetch;
        auto old_prediction = fetch->get_prediction();
        auto old_line_prediction = fetch->line_prediction;

        auto& event_stream = get_environment()->event_stream;
        bool event_stream_enabled = event_stream->is_enabled();
        get_environment()->event_stream->disable();
        tage.predict_inst(fetch, history, br_context);
        if (event_stream_enabled)
            get_environment()->event_stream->enable();

        if (fetch->get_prediction().is_taken() == br_info.taken && fetch->get_prediction().target == br_info.target)
            ev_pipeline_mispredict(bob_entry.transaction());

        fetch->update_prediction(old_prediction);
        fetch->line_prediction = old_line_prediction;
    }

    tage.commit_branch(br_info);

    if (br_train.is_enabled()) {
        std::string history_string{};
        std::string compressed_history_string{};

        if (post_branch_history) {
            auto predictor =
                (br_info.slot.use_alternate ? br_info.slot.alternate.predictor : br_info.slot.primary.predictor);
            auto pred_obj = dynamic_cast<core::BPGlobal*>(predictor);
            unsigned depth = pred_obj ? pred_obj->history_size + 1 : 2;

            history_string = history.history_as_string(br_info.fetch->fetch_id, depth);
            compressed_history_string = history.compressed_history_as_string(br_info.fetch->fetch_id);
        }

        br_train(bob_entry.transaction(),
                 br_info.pc,
                 br_type(br_info.type),
                 br_target(br_info.target),
                 br_target_nextline(br_info.target_nextline),
                 br_taken(br_info.taken),
                 br_btb_miss(br_info.btb_miss),
                 br_miss_all(br_info.miss_all),
                 br_mispredicted(br_info.mispredicted_direction || br_info.mispredicted_target),
                 br_mispredicted_direction(br_info.mispredicted_direction),
                 br_mispredicted_target(br_info.mispredicted_target),
                 br_primary_predictor(predictor_name(br_info, false)),
                 br_alternate_predictor(predictor_name(br_info, true)),
                 br_use_alternate(br_info.slot.use_alternate),
                 br_update_type(update_state),
                 br_history(history_string),
                 cbhr(compressed_history_string));
    }

    ld.train(br_info, history, br_context);

    return true;
}

void BPU::handle_allocations()
{
    while (bob_alloc->can_take()) {
        auto uop = bob_alloc->take();

        auto [it, inserted] = bob.emplace(
            std::piecewise_construct, std::forward_as_tuple(uop->alloc_id), std::forward_as_tuple(uop, context));
        auto& entry = it->second;
        const BranchInfo& br_info = entry.br_info;

        if (!inserted)
            AMPERE_ERROR("BPU was given a duplicate alloc_id ({}) to place in the BOB", uop->alloc_id);

        br_dispatch(uop,
                    br_type(entry.branch_type()),
                    br_btb_miss(br_info.btb_miss),
                    br_miss_all(br_info.miss_all),
                    br_rsb_hit(br_info.rsb_hit),
                    br_ittage_hit(br_info.ittage_hit));

        if (br_info.ittage_hit)
            pmu_event(pmu_num_data(archx::PMU::BPU_HIT_INDIRECT_PREDICTOR_ITTAGE));

        if (((br_info.btb_miss && is_indirect(br_info.type) && !is_return(br_info.type)))
            || (!br_info.rsb_hit && is_return(br_info.type)))
            pmu_event(pmu_num_data(archx::PMU::BPU_IND_NO_PREDICT));
    }
}

bool BPU::handle_flush()
{
    if (flush->can_read()) {
        auto flush_info = flush->read();
        auto alloc_id = flush_info->alloc_id;

        if (flush_info->type == FlushType::RAW_SCRUB || flush_info->type == FlushType::CSF_NO_CMT)
            alloc_id += 1;

        auto bob_entry_p = bob.find(alloc_id);
        BOBEntry* bob_entry = bob_entry_p == bob.end() ? nullptr : &bob_entry_p->second;

        if (flush_info->type == FlushType::BR_MISPREDICT && bob_entry) {
            BranchInfo& br_info = bob_entry->br_info;
            if (flush_info->mispredicted_direction)
                br_info.taken = !br_info.taken;

            br_info.mispredicted_direction = flush_info->mispredicted_direction;
            br_info.mispredicted_target = flush_info->mispredicted_target;

            if (br_info.mispredicted_target)
                br_info.target = flush_info->instruction->archx_instruction->branch_target();
        } else if (flush_info->type == FlushType::NOT_KNOWN
                   && flush_info->instruction->archx_instruction->is_taken_branch())
        {
            bob_entry->br_info.target = flush_info->instruction->archx_instruction->branch_target();
        }

        recover(flush_info, bob_entry);

        // This might delete the entry pointed to by bob_entry, so it has to be done after recover()
        flush_bob_entries(flush_info->alloc_id + 1);

        bob_alloc->clear();

        // Increment the FEQ throttler flush counter and record that we're in
        // the shadow of a flush
        feq_throttle_flushes++;
        feq_throttle_flush_shadow = true;

        return true;
    }

    return false;
}

bool BPU::handle_branch_check(bool gpc_flush)
{
    Flush br_check_flush_info;
    if (br_check_flush->can_take())
        br_check_flush_info = br_check_flush->take();

    // Ignore branch check if we also received a GPC flush
    if (!gpc_flush && br_check_flush_info) {
        recover(br_check_flush_info, nullptr);
        return true;
    }

    return false;
}

void BPU::handle_loop_history()
{
    while (loop_fetch->can_take()) {
        auto fetch = loop_fetch->take();
        AMPERE_ASSERT(fetch->fetch_id >= current_fetch_id,
                      "BPU attempting to set current_fetch_id to {}, which is less than current value of {}",
                      fetch->fetch_id,
                      current_fetch_id);
        current_fetch_id = fetch->fetch_id + 1;
        if (update_history_while_looping)
            update_history(fetch);
    }
}

void BPU::handle_loop_restart()
{
    if (loop_restart->can_take()) {
        auto restart = loop_restart->take();
        update_pc(restart->target_pc, true);
        prev_pc = restart->pc;
        prev_prev_pc = restart->prev_pc;
        prev_bank = restart->bank;

        ld.loop_restart(restart);
    }
}

void BPU::recover(Flush& flush_info, BOBEntry* bob_entry)
{
    inum_t recovery_id;
    // RAW scrub flushes will restart from 'scrubbed' instruction not next instruction.
    if (flush_info->type == FlushType::RAW_SCRUB || flush_info->type == FlushType::CSF_NO_CMT) {
        // this allows us to refetch the load that suffered the raw scrub or the CSF_NO_CMT instruction that
        // needs to re-execute. The recovery() interface advances the identifier.
        recovery_id = flush_info->instruction->prev_inum();
    } else {
        recovery_id = flush_info->instruction->inum();
    }

    flush_info->is_context_change = update_context();

    mispredicted_el0_path = (context.el == archx::EL::EL0) && recovery_id.wrong_path();
    bool recover_perfectly = (context.el == archx::EL::EL0) && !recovery_id.wrong_path();

    if (perfect) {
        driver->recover(recovery_id);
        stalling_fetch_address.reset();
        update_pc(driver->next_pc(), true);
        return;
    }

    BranchType branch_type = BranchType::NONE;
    ProgramCounter branch_pc;
    std::optional<ProgramCounter> target;
    bool taken{false};

    if (flush_info->type == FlushType::BR_MISPREDICT) {
        branch_pc = flush_info->flushing_pc;
        target = flush_info->pc;
        AMPERE_ASSERT(bob_entry, "We must have a BOB entry for every mispredicted branch");
        branch_type = bob_entry->br_info.type;
        taken = bob_entry->br_info.taken;
    } else if (flush_info->is_branch_check()) {
        branch_pc = flush_info->flushing_pc;
        target = flush_info->pc;
        taken = flush_info->type != FlushType::BRANCH_CHECK_WRONG_TYPE;

        switch (flush_info->type) {
        case FlushType::BRANCH_CHECK_UNCOND_BRANCH:
            branch_type = BranchType::UNCONDITIONAL_DIRECT;
            break;
        case FlushType::BRANCH_CHECK_COND_BRANCH:
            branch_type = BranchType::CONDITIONAL_DIRECT;
            break;
        case FlushType::BRANCH_CHECK_CALL_DIRECT:
            branch_type = BranchType::CALL_DIRECT;
            break;
        case FlushType::BRANCH_CHECK_CALL_INDIRECT:
            branch_type = BranchType::CALL_INDIRECT;
            break;
        case FlushType::BRANCH_CHECK_UNCOND_INDIRECT:
            branch_type = BranchType::UNCONDITIONAL_INDIRECT;
            break;
        case FlushType::BRANCH_CHECK_RETURN:
            branch_type = BranchType::RETURN;
            break;
        case FlushType::BRANCH_CHECK_TARGET:
            // Technically we don't know if this was conditional or not,
            // but it must have been taken, and we fix up the history the
            // same either way.
            //
            branch_type = BranchType::UNCONDITIONAL_DIRECT;
            break;
        default:
            branch_type = BranchType::NONE;
            break;
        }
    }

    flush_history(flush_info, branch_type, branch_pc, target, taken);

    driver->recover(recovery_id);

    ProgramCounter flush_target;
    if (perfect_el0 && recover_perfectly) {
        // Reset the Loop Detector when we're recovering in perfect EL0 -  this prevents
        // the Loop Detector getting deadlocked when we are looping while0 transitioning
        // from EL1 (prediction state) to EL0 (perfect BPU state)
        ld.recover(flush_info, nullptr, history, context);
        flush_target = driver->next_pc();
    } else {
        if (bob_entry) {
            tage.recover(flush_info, &bob_entry->br_info, history, context);
            ld.recover(flush_info, &bob_entry->br_info, history, context);
            br_recover(bob_entry->transaction(),
                       flush_info->pc,
                       br_pc(bob_entry->pc()),
                       br_type(bob_entry->branch_type()),
                       br_btb_miss(bob_entry->br_info.btb_miss),
                       br_rsb_hit(bob_entry->br_info.rsb_hit),
                       br_ittage_hit(bob_entry->br_info.ittage_hit),
                       br_miss_all(bob_entry->br_info.miss_all));
        } else {
            if (flush_info->type != FlushType::BR_MISPREDICT)
                tage.recover(flush_info, nullptr, history, context);
            ld.recover(flush_info, nullptr, history, context);
            br_recover(flush_info->pc, br_type(BranchType::NONE));
        }
        flush_target = flush_info->pc;
    }

    update_pc(flush_target, true);

    // Determine whether this *could have* been the same prediction as the
    // prediction preceding it had the clear/flush not occurred. If so, we want
    // to keep our BTB/TAGE bank the same as that previous prediction to ensure
    // we train consistently.
    bool same_consistent_prediction = !taken && flush_target.line_offset() != 0 && !flush_info->is_context_change;
    reuse_bank = same_consistent_prediction;
    prev_bank = flush_info->fetch->bank;

    stalling_fetch_address.reset();
    prev_pc = flush_info->fetch->prev_program_counter;
    prev_prev_pc = flush_info->fetch->prev_prev_program_counter;

    if (np_on_flush) {
        next_early_predictor = EarlyPredictor::NP;
    }
}

void BPU::flush_bob_entries(std::uint64_t alloc_id)
{
    for (auto entry = bob.lower_bound(alloc_id); entry != bob.end();) {
        auto bobid = entry->second.extract_bobid();
        release_credit(bobid);
        auto next_entry = entry;
        ++next_entry;
        bob.erase(entry);
        entry = next_entry;
    }
}

void BPU::flush_history(Flush& flush_info,
                        BranchType branch_type,
                        ProgramCounter branch_pc,
                        std::optional<ProgramCounter> target,
                        bool taken)
{
    history.flush(flush_info->fetch->fetch_id);

    if (is_conditional(branch_type) && taken)
        history.push(flush_info->fetch->fetch_id, branch_pc);
    else if (is_indirect(branch_type)) {
        AMPERE_ASSERT(target.has_value(), "BPPU::flush_history called for indirect branch without target set");
        history.push(flush_info->fetch->fetch_id, branch_pc, target.value());
    } else if (is_unconditional(branch_type))
        history.push(flush_info->fetch->fetch_id, branch_pc);
}

std::string BPU::predictor_name(const BranchInfo& br_info, bool alternate) const
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

void BPU::dump(std::ostream& os) const
{
    using namespace archx;

    os << "= BPU ======================================================\n";
    os << "BOB:\n";
    os << "|  # |   Alloc Id   |   Program  Counter   |        Target        |  MisDir  |  MisTgt  |\n";
    int idx = 0;
    for (auto& bob_entry : bob) {
        auto& branch_info = bob_entry.second.br_info;
        fmt::print("| {:>2d} | {:>12} | {:^#020x} | {:^#020x} | {:^8} | {:^8} |\n",
                   idx++,
                   bob_entry.second.alloc_id(),
                   branch_info.pc.value(),
                   branch_info.target.value(),
                   branch_info.mispredicted_direction ? "true" : "false",
                   branch_info.mispredicted_target ? "true" : "false");
    }
    fmt::print("FEQ credits: {}\n", feq.get_count());
    os << "============================================================\n";
}

static std::string line_prediction_as_string(const LinePrediction& line_prediction)
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

} // namespace ampere::core

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
