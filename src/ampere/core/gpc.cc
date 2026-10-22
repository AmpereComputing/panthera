//
// Copyright (c) 2018-2024 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <fmt/format.h>
#include <cstdint>
#include "ampere/core/gpc.h"
#include "ampere/core/ixu_defs.h"
#include "ampere/archx/pmu.h"
#include "ampere/archx/translation_debug.h"

namespace ampere::core {
// #define DEBUG_FLSH
#if defined(DEBUG_FLSH)
#define DBGFLSH(format_str, ...) fmt::print(stderr, "{}: " format_str "\n", fmt_cycle(), ##__VA_ARGS__)
#else
#define DBGFLSH(format_str, ...) \
    do {                         \
    } while (0)
#endif
// #define DEBUG_COMMIT
#if defined(DEBUG_COMMIT)
#define DBGCMT(format_str, ...) fmt::print(stderr, "{}: " format_str "\n", fmt_cycle(), ##__VA_ARGS__)
#else
#define DBGCMT(format_str, ...) \
    do {                        \
    } while (0)
#endif

GPC::GPC(const sc_core::sc_module_name& name) : Module(name)
{
    warmup_instrs_retired = 0;
    wall_time_start[0] = std::chrono::steady_clock::now();
    // Measure process cpu time (on linux), not wall-clock time.
    // This allows us to ignore "suspend" time in slurm.
    cpu_time_start[0] = std::clock();

    connect(commit_delay_out_port, commit_delay_in_port).name("commit_delay");

    // The commit delay and commit bypass features don't play well together
    if (commit_bypass) {
        AMPERE_ASSERT(!(enable_fsu_commit_delay || enable_lsu_commit_delay || enable_ixu_commit_delay),
                      "Can't enable commit bypass and commit delays at the same time");
    }

    if (warmup_instructions == 0)
        start_measurement();
}

bool GPC::run()
{
    // CPU_CYCLES is computed in rollup_formula.py as equal to ev_cycle
    if (!rob.empty()) {
        auto uop = rob.front();
        if (!uop->wrong_path())
            curr_el = uop->exception_level();
    }
    if (start_measurement_cycle)
        warmup_state = false;
    ev_cycle(ev_el(curr_el), ev_warmup(warmup_state));
    flush_id = std::nullopt; // was {}
    if (flush_read_port->can_read()) {
        auto flush_tx = flush_read_port->read();
        flush(flush_tx->alloc_id);
        flush_id = flush_tx->alloc_id;
        ev_flush_broadcast(flush_tx);
        DBGFLSH("GPC sees flush from flush_read_port. flush_id: {}", flush_id.value());
        // After a flush we can't commit anything for 6 cycles. That's not really any different for a NO_RESTART
        // flush, but starting with Connor the non-spec flush after NO_RESTART will happen within that window,
        // so to get the latency right we subtract some cycles here.
        auto last_flushed_cycle = get_cycle();
        if (flush_tx->type == FlushType::NO_RESTART)
            commit_freeze_ends = last_flushed_cycle + no_restart_commit_freeze;
        else
            commit_freeze_ends = last_flushed_cycle + commit_freeze_cycles;
    }

    if (rob.empty() && idr_requested_flush) {
        if (idr_flush_uop->swob_occupancy) {
            // If IDR wants a flush but there is something in the SWOB, we have to drain it first
            DBGFLSH("FLUSH 1");
            generate_flush(idr_flush_uop, FlushType::NO_RESTART, true, false);
        } else {
            // After the no-restart flush, we need to wait for the SWOB drain to complete before doing the IDR-requested
            // flush
            if (swob_drain_timer) {
                --swob_drain_timer;
                return true;
            }
            DBGFLSH("FLUSH 2");
            generate_flush(idr_flush_uop, FlushType::CSF_NO_CMT, true, false);
        }
        return true;
    }

    if (flush_req_from_idr->can_take()) {
        idr_flush_uop = flush_req_from_idr->take();
        idr_requested_flush = true;
    }

    // When a flush is signalled, the GPC uses all the possible IDR credit
    // bandwidth for recovering, and thus commit must freeze for a few cycles
    // since there is no bandwidth available to commit.
    //
    // If we are about to signal a flush, hold off on commit, otherwise we commit things out of order
    //
    bool can_commit = allowed_to_commit();
    if (!can_commit || (commit_freeze_cycles == 0 && get_cycle() == commit_freeze_ends)) {
        // we still have to allow the older branches to report and generate
        // flushes normally. younger branches can be ignored. Same is true for raw_scrubs and the older uop between
        // the branch and raw_scrub should generate flush.
        auto mispredicted_branch = process_mispredicts();
        auto raw_scrub = process_raw_scrubs();
        if (mispredicted_branch || raw_scrub) {
            auto older_flusher = select_older_flusher(mispredicted_branch, raw_scrub);

            // Must allow secondary flushes. When doing wrong path
            // emulation, we can have a wrong-path raw scrub trigger a
            // flush before the branch mispredict resolves.
            DBGFLSH("FLUSH 3");
            generate_flush(older_flusher.first, older_flusher.second, false, false);
        }
    }

    if (can_commit)
        commit(uop_retire_width, false);

    dispatch();
    report();

    // Don't call the ROB empty if we are in the middle of a SWOB drain -- barriers should not proceed during this
    // window
    if (rob_was_empty != rob.empty() && swob_drain_timer == 0) {
        rob_empty_out->put(rob.empty());
        rob_was_empty = rob.empty();
    }

    check_for_braindeath();

    return true;
}

void GPC::report()
{
    bool check_for_bypass{false};

    // save the initial information for the slot0 uop
    if (!rob.empty()) {
        auto uop = rob.front();
        check_for_bypass = !uop->complete;
    }

    // IXU reports that uops are complete
    while (commit_port->can_take()) {
        auto uop = commit_port->take();
        if ((enable_ixu_commit_delay && uop->is_ixu()) || (enable_lsu_commit_delay && uop->is_lsu())
            || (enable_fsu_commit_delay && uop->is_fsu()))
        {
            commit_delay_out_port->put(uop);
        } else {
            AMPERE_ASSERT(uop->complete == false,
                          "{}: Duplicate uop completion for txid: {}, allocid {}, {:016x}: {}",
                          fmt_cycle(),
                          uop->get_txid(),
                          uop->get_alloc_id(),
                          uop->pc().value(),
                          uop->uop_disasm());
            uop->complete = true;
        }
    }

    // After the commit delay uops are complete and ready to commit
    while (commit_delay_in_port->can_take()) {
        auto uop = commit_delay_in_port->take();
        AMPERE_ASSERT(uop->complete == false,
                      "{}: (Delayed) Duplicate uop completion for txid: {}, allocid: {}, pc: {:016x}, {}",
                      fmt_cycle(),
                      uop->get_txid(),
                      uop->get_alloc_id(),
                      uop->pc().value(),
                      uop->uop_disasm());
        uop->complete = true;
    }

    // if the commit_bypass parameter is false, just return
    if (!commit_bypass)
        return;

    // if we retired uops this cycle, just return
    if (uops_retired_this_cycle)
        return;

    // if slot0 was already complete, no bypass, just return
    if (!check_for_bypass)
        return;

    // if slot0 just got marked as complete this cycle, try to bypass
    if (rob.front()->complete) {
        // we know we have 1. we can check for 2,3,4
        uint count;

        // count how many uops are complete at the top of the ROB
        auto it = rob.begin();
        Uop uop;
        for (count = 0; count < uop_retire_width; count++) {
            uop = *it;

            if (!uop->complete)
                break;

            it++;
            if (it == rob.end())
                break;
        }

        // we can commit some uops on the bypass path
        ev_commit_bypass(rob.front());
        commit(count, true);
    }
}

void GPC::dispatch()
{
    while (dispatch_port->can_take()) {
        auto uop = dispatch_port->take();
        rob.push_back(uop);
    }
}

bool GPC::ready_to_commit(const Uop& uop) const
{
    if (!uop->complete)
        return false;

    // FSU ops can commit N cycles after issue, no sooner
    // FIXME: FS12 on <= Ripley, and FS11 on >= Connor
    Cycle curr_cycle = get_cycle();
    if (Pipe::X == uop->sch.pipe_type || Pipe::Y == uop->sch.pipe_type) {
        CycleDelta cd = curr_cycle - uop->issue_cycle;
        if (cd < fsu_commit_latency)
            return false;
    } else if (Pipe::Z == uop->sch.pipe_type) {
        // delay FSU Z pipe uops until FS05
        const CycleDelta cd = curr_cycle - uop->issue_cycle;
        if (cd < fsu_commit_latency_z)
            return false;
    }

    // Do not commit the uop if it needs to flush due to a RAW scrub -- the signal to flush
    // should be arriving shortly from LSU
    if (uop->raw_scrubbed)
        return false;

    // What really happens in hardware is:
    // - Non-spec exceptions can only happen from ROB lane 0 (first uop in a cycle to commit)
    // - A uop which needs a non-spec exception waits an extra cycle at the head of the ROB before committing
    // - The non-spec flush is signaled the cycle *after* the uop commits
    //
    // Signaling the flush the cycle after commit gets really messy, so instead we just delay commit of
    // the flushing uop by 3 cycles, then commit and flush in the same cycle.
    if (uop->requires_flush() && get_cycle() - last_commit_cycle < nonspec_flush_delay && !uop->cleaved_second_half)
        return false;

    return true;
}

void GPC::commit(uint width, bool bypass)
{
    uint instructions_committed{0}, loads_committed{0}, stores_committed{0}, branches_committed{0},
        int_dsts_committed{0}, vec_dsts_committed{0};
    Uop mispredicted_branch = bypass ? nullptr : process_mispredicts();
    Uop raw_scrub = process_raw_scrubs();
    Uop flushing_uop;
    std::optional<archx::FlushType> flush_type;
    uops_retired_this_cycle = 0;

    // After a context-synchronizing flush which needed to drain the SWOB, wait an empirically determined time
    // before resuming commit.
    if (swob_drain_timer) {
        --swob_drain_timer;
        return;
    }

    for (size_t i = 0; !rob.empty() && i < width;) {
        unsigned int_dsts{0};
        unsigned vec_dsts{0};
        unsigned branches{0};
        auto uop = rob.front();

        int_dsts += uop->int_dests();
        vec_dsts += uop->vec_dests();
        branches += uop->is_branch() ? 1 : 0;

        // We can't commit the first half of a cleaved pair until both halves are done, because in real life they
        // are a single uop in the ROB.
        bool ready = ready_to_commit(uop);
        if (uop->cleaved_first_half && !allow_partial_cleaving) {
            AMPERE_ASSERT(rob.size() > 1 && rob[1]->cleaved_second_half,
                          "GPC saw first half of cleaved pair without the second half");
            ready = ready && ready_to_commit(rob[1]);
            int_dsts += rob[1]->int_dests();
            vec_dsts += rob[1]->vec_dests();
            branches += rob[1]->is_branch() ? 1 : 0;
        }

        if (!ready)
            break;

        if (int_dsts_committed + int_dsts > commit_intdsts_per_cycle
            || vec_dsts_committed + vec_dsts > commit_vecdsts_per_cycle
            || branches_committed + branches > commit_branches)
            break;

        // We should never get a mispredict signal for a uop in the past
        if (mispredicted_branch)
            AMPERE_ASSERT(mispredicted_branch->get_alloc_id() > uop->get_alloc_id(),
                          "GPC saw mispredict for a uop that committed or is about to commit");

        // A non-speculative flush must first drain the SWOB if there is anything in it
        if (uop->requires_flush() && uop->swob_occupancy) {
            flushing_uop = uop;
            flush_type = FlushType::NO_RESTART;
            break;
        }

        AMPERE_ASSERT(!(uop->cleaved_second_half && uops_retired_this_cycle == 0) || allow_partial_cleaving,
                      "Committing second cleavelet at the beginning of a cycle");

        commit_uop(uop);
        DBGCMT("COMMIT inum {}, txid {}, allocid {}, pc {}, {} for {:08x} {}",
               uop->inum().id(),
               uop->get_txid(),
               uop->get_alloc_id(),
               uop->pc(),
               uop->uop_disasm(),
               uop->ainst()->opcode(),
               uop->a64_disasm());
        if (uop->is_gather()) {
            DBG_GATHERC("GPC Committing gather. parent txid {}, txid {}, allocid {}",
                        uop->get_txid_parent(),
                        uop->get_txid(),
                        uop->get_alloc_id());
        }

        if (uop->is_load()) // includes gathers
            loads_committed++;

        if ((uop->is_single_uop_store() && !uop->is_pair()) || (uop->is_ixu_store_data() && !uop->is_pair())
            || uop->is_single_uop_store_pair() || uop->is_ixu_store_high() || uop->is_fsu_store_low()
            || // this includes all STDV (including from scatters)
            uop->is_store_special() || uop->is_sw_prefetch_as_sta())
        {
            stores_committed++;
        }

        if (uop->archx_uop->end_inst) {
            instrs_since_last_branch += uop->instructions.size();
            instructions_committed += uop->instructions.size();
        }

        if (uop->is_branch()) {
            uop->instrs_since_last_branch = instrs_since_last_branch;
            instrs_since_last_branch = 0;
            branches_committed++;
        }

        rob.pop_front();
        uops_retired_this_cycle++;
        int_dsts_committed += uop->int_dests();
        vec_dsts_committed += uop->vec_dests();

        // Don't retire any more uops this cycle if this uop requires a flush
        auto flush_required = uop->requires_flush();
        if (flush_required) {
            flush_type = flush_required;
            flushing_uop = uop;
            break;
        }

        // The first half of a cleaved uop doesn't have a ROB credit, because it shares the ROB entry with the second
        // half. So it doesn't count toward the number of uops we can commit this cycle.
        if (uop->robid || rob.empty() || !rob.front()->cleaved_second_half)
            i++;
    }

    if (loads_committed)
        loads_committed_port->put(loads_committed);

    if (stores_committed)
        stores_committed_port->put(stores_committed);

    // If a LSU uop is now at the head of the ROB (and it's not the same uop
    // that was already there), tell the LSU about it. If the first two uops in the
    // ROB are cleaved, then the second half of the pair is considered to be at the head.
    if (!rob.empty()) {
        Uop head = rob.front();
        if (head->cleaved_first_half && head->complete && rob.size() > 1)
            head = rob[1];
        if (head->is_lsu() && head->get_alloc_id() != last_lsu_uop_id) {
            lsu_next_to_commit_port->put(head->get_alloc_id());
            last_lsu_uop_id = head->get_alloc_id();
        }
    }

    if (instructions_committed)
        instructions_committed_port->put(instructions_committed);

    if (branches_committed)
        branches_committed_port->put(branches_committed);

    if (flushing_uop) {
        if (*flush_type == FlushType::NO_RESTART) {
            DBGFLSH("FLUSH 4");
            generate_flush(flushing_uop, *flush_type, true, false);
        } else {
            DBGFLSH("FLUSH 5");
            generate_flush(flushing_uop, *flush_type, false, true);
        }
    } else if (mispredicted_branch || raw_scrub) {
        auto older_flusher = select_older_flusher(mispredicted_branch, raw_scrub);

        if (!flush_id || older_flusher.first->is_older(*flush_id)) {
            DBGFLSH("FLUSH 6. mispred {}, rawscrub {}, flush_id = {:d}, older_flusher id {}, mispred_br id {:d}",
                    (mispredicted_branch != nullptr),
                    (raw_scrub != nullptr),
                    (int)flush_id.value_or(-1),
                    older_flusher.first->get_alloc_id(),
                    (int)(mispredicted_branch ? mispredicted_branch->get_alloc_id() : -1));
            if (mispredicted_branch) {
                DBGFLSH("\tMISPREDICTED BR:  iid: {}, allocid: {}, pc: {}, disasm: {}",
                        mispredicted_branch->inum().id(),
                        mispredicted_branch->get_alloc_id(),
                        mispredicted_branch->pc(),
                        mispredicted_branch->a64_disasm());
            }
            if (raw_scrub) {
                DBGFLSH("\tRAW SCRUB:  iid: {}, allocid: {}, pc: {}, disasm: {}",
                        raw_scrub->inum().id(),
                        raw_scrub->get_alloc_id(),
                        raw_scrub->pc(),
                        raw_scrub->a64_disasm());
            }
            generate_flush(older_flusher.first, older_flusher.second, false, false);
        }
    }
}

void GPC::uop_commit_checker(const Uop& uop)
{
    if (instrs_retired && !disable_translation_checker && (uop->is_load() || uop->is_store_address())) {
        if (!uop->phys_addr && !uop->is_sw_prefetch() && !uop->is_predicated_off()) {
            std::string msg(
                fmt::format("Uop {:#d} committed without phys_addr"
                            " for {} at pc {:#x} with virt_addr {:#x}\n"
                            " expected: {:#x}",
                            uop->get_alloc_id(),
                            uop->is_load() ? "load" : "store",
                            uop->pc().value(),
                            uop->virt_addr.value_or(VirtAddr(0xdeadbeef)).value(),
                            uop->instructions.front()->archx_instruction->physical_address().value()));
            if (uop->virt_addr)
                msg += "\n" + archx::debug_translation(driver_port, uop->virt_addr.value(), false /*data*/);
            AMPERE_ERROR(fmt::runtime(msg));
        }

        // We only check the first memory uop of each instruction, because later uops will (correctly) have
        // different addresses, and the archx instruction only knows about the first address touched by
        // each instruction. Skip uops for instructions that take exceptions because they don't have
        // VA/PA in our traces.
        if (uop->phys_addr && uop->first_mem_uop && !uop->faulted && !uop->is_dc_zva() && !uop->instr_has_exception()
            && !uop->is_predicated() && !uop->is_gather() && !uop->is_scatter()
            && uop->phys_addr.value()
                   != uop->instructions.front()->archx_instruction->physical_address() + phyaddroffset)
        {
            std::string msg(
                fmt::format("Uop {:#d} committed with unexpected"
                            " phys_addr for {} at pc {:#x} with virt_addr {:#x}\n"
                            " expected: {:#x}\n"
                            " modeled:  {:#x}",
                            uop->get_alloc_id(),
                            uop->is_load() ? "load" : "store",
                            uop->pc().value(),
                            uop->virt_addr.value_or(VirtAddr(0xdeadbeef)).value(),
                            uop->instructions.front()->archx_instruction->physical_address().value() + phyaddroffset,
                            uop->phys_addr.value_or(PhysAddr(0xdeadbeef)).value()));
            if (uop->virt_addr)
                msg += "\n" + archx::debug_translation(driver_port, uop->virt_addr.value(), false /*data*/);
            AMPERE_ERROR(fmt::runtime(msg));
        }
    }
}

void GPC::commit_uop(const Uop& uop)
{
    if (uop->robid) {
        if (start_measurement_cycle)
            uops_retired_el[uop->exception_level()] = uops_retired_el[uop->exception_level()].value_or(0) + 1;
        else
            warmup_uops_retired_el[uop->exception_level()] =
                warmup_uops_retired_el[uop->exception_level()].value_or(0) + 1;
        ++uops_committed;
    }

    ev_uop_commit(uop,
                  commit_pc_data(uop->pc().value()),
                  commit_delay_data(get_cycle() - last_commit_cycle),
                  ev_predicated_off(uop->is_predicated_off()),
                  ev_cleaved(uop->cleaved_second_half),
                  ev_el(uop->exception_level()),
                  ev_warmup(warmup_state),
                  ev_robid(uop->rob_entry));

    if (uop->is_idiv()) {
        ev_idiv_commit(uop);
    } else if (uop->is_fdiv()) {
        ev_fdiv_commit(uop);
    } else if (uop->is_fsqrt()) {
        ev_fsqrt_commit(uop);
    }

    if (!uop->cleaved_second_half)
        ev_pmu(uop, ev_num(archx::PMU::OP_RETIRED));

    // This gets its own event to make it easy to measure cleaving
    if (uop->cleaved_second_half)
        ev_cleaved_uop_commit(uop);

    // Return credits
    idr_uop_return->put(uop);

    if (uop->archx_uop->end_inst) {
        for (auto inst : uop->instructions) {
            commit_instruction(inst);
        }
    }

    if (uop->cleaved_second_half)
        AMPERE_ASSERT(uop->rob_entry == last_committed_robid,
                      "Second half of cleaved pair does not have same ROB ID as first half");
    else if (uop->rob_entry != 0)
        AMPERE_ASSERT((uop->rob_entry == last_committed_robid + 1)
                          || (allow_partial_cleaving && uop->rob_entry == last_committed_robid),
                      "Non-consecutive ROB IDs from {} to {}\n",
                      last_committed_robid,
                      uop->rob_entry);
    last_committed_robid = uop->rob_entry;

    last_commit_cycle = get_cycle();
    last_committed_uop = uop;

    uop_commit_checker(uop);
}

void GPC::commit_instruction(const Instruction& instruction)
{
    ev_instruction_commit(instruction,
                          commit_pc_data(instruction->pc().value()),
                          ev_inum(instruction->inum()),
                          ev_emulated(instruction->emulated()),
                          ev_el(instruction->exception_level()),
                          ev_warmup(warmup_state));

    const std::uint32_t hint_0x54 = 0xd5032a9f;
    if (instruction->archx_instruction->opcode() == hint_0x54) {
        ev_hint_0x54_commit(instruction, commit_pc_data(instruction->pc().value()), ev_inum(instruction->inum()));
    }

    if (instruction->looping)
        ev_looped_instruction_commit(instruction);
    instruction_commit_checker(instruction);
    architectural_commit_pmus(instruction);
    driver_port->commit(instruction->archx_instruction);

    auto cycles = get_cycle() - last_instr_commit_cycle;
    if (start_measurement_cycle) {
        cycles_el[instruction->exception_level()] = cycles_el[instruction->exception_level()].value_or(0) + cycles;
        instrs_retired = *instrs_retired + 1;
        instrs_retired_el[instruction->exception_level()] =
            instrs_retired_el[instruction->exception_level()].value_or(0) + 1;

    } else {
        warmup_cycles_el[instruction->exception_level()] =
            warmup_cycles_el[instruction->exception_level()].value_or(0) + cycles;
        warmup_instrs_retired = *warmup_instrs_retired + 1;
        warmup_instrs_retired_el[instruction->exception_level()] =
            warmup_instrs_retired_el[instruction->exception_level()].value_or(0) + 1;
    }

    if (instrs_retired && *instrs_retired % heartbeat == 0)
        print_heartbeat("Simulate", *instrs_retired);
    else if (!start_measurement_cycle && warmup_instrs_retired && *warmup_instrs_retired % heartbeat == 0)
        print_heartbeat("Warmup  ", *warmup_instrs_retired);

    if (instruction->archx_instruction->control())
        simulation_control(instruction, warmup_instructions.has_value());

    if (instrs_retired && *instrs_retired >= max_instructions) {
        if (!stopped) {
            AMPERE_WARNING("Maximum instruction count ({}) reached @ {}", max_instructions, sc_core::sc_time_stamp());
            stop_measurement(&ev_stop_max_instruction);
        }
    } else if (warmup_instrs_retired >= max_warmup_instructions) {
        if (!stopped) {
            AMPERE_WARNING("Maximum warmup instruction count ({}) reached @ {}",
                           max_warmup_instructions,
                           sc_core::sc_time_stamp());
            stop_measurement(&ev_stop_max_instruction);
        }
    } else if (warmup_instructions && !start_measurement_cycle && warmup_instrs_retired >= *warmup_instructions) {
        start_measurement(warmup_instrs_retired > *warmup_instructions ? instruction : nullptr);
    }

    last_instr_commit_cycle = get_cycle();
}

void GPC::architectural_commit_pmus(const Instruction& instruction)
{
    auto& archx_instr = instruction->archx_instruction;
    auto archx_uops = archx_instr->decode();
    bool store = false, load = false;
    for (auto& uop : archx_uops) {
        if (uop->info->unit == archx::Uopunit::Store) {
            store = true;
        }
        if (uop->info->unit == archx::Uopunit::Load) {
            load = true;
        }
    }

    // INST_RETIRED is computed in rollup_formula.py as equal to instruction_commit
    if (store)
        ev_pmu(instruction, ev_num(archx::PMU::ST_RETIRED));
    if (load)
        ev_pmu(instruction, ev_num(archx::PMU::LD_RETIRED));

    if (archx_instr->is_sve_simd()) {
        // SVE simd (exclude SVE non-simd)
        ev_pmu(instruction, ev_num(archx::PMU::SVE_INST_RETIRED));
    }
    if (archx_instr->is_ase_simd()) {
        // AdvSIMD (ASE) simd (exclude scalar, implementation defined choice on Trinity)
        ev_pmu(instruction, ev_num(archx::PMU::ASE_INST_RETIRED));
    }
    if (archx_instr->is_sve_simd() || archx_instr->is_ase_simd()) {
        // SVE and AdvSIMD(ASE)
        ev_pmu(instruction, ev_num(archx::PMU::ASE_SVE_INST_RETIRED));
    }
    if (archx_instr->is_sve_simd() || archx_instr->is_ase_simd()) {
        // count all SIMD instructions:
        // - SVE instrcutions, except non-SIMD SVE
        // - AdvSIMD instructions, except AdvSIMD scalars
        ev_pmu(instruction, ev_num(archx::PMU::SIMD_INST_RETIRED));
    }

    if (archx_instr->is_branch())
        architectural_commit_branch_pmus(instruction);
    if (archx_instr->is_eret())
        ev_pmu(instruction, ev_num(archx::PMU::EXC_RETURN));
    if (archx_instr->is_msr_reg()) {
        auto sysreg = archx_instr->msr_sysreg();
        if (sysreg == archx::TTBR0_EL1 || sysreg == archx::TTBR1_EL1)
            ev_pmu(instruction, ev_num(archx::PMU::TTBR_WRITE_RETIRED));
        else if (sysreg == archx::CONTEXTIDR_EL1)
            ev_pmu(instruction, ev_num(archx::PMU::CID_WRITE_RETIRED));
    }
}

void GPC::architectural_commit_branch_pmus(const Instruction& instruction)
{
    enum {
        TAKEN = 1,
        MIS_PRED = 2,
        IND = 4,
        RETURN = 8,
        COND = 16
    };

    const struct EventInfo
    {
        int mask;
        int value;
        PMU event;
    } events[] = {
        {.mask = 0, .value = 0, .event = archx::PMU::BR_RETIRED},
        {.mask = TAKEN, .value = 0, .event = archx::PMU::BR_SKIP_RETIRED},
        {.mask = TAKEN | MIS_PRED, .value = 0, .event = archx::PMU::BR_SKIP_PRED_RETIRED},
        {.mask = TAKEN | MIS_PRED, .value = TAKEN, .event = archx::PMU::BR_TAKEN_PRED_RETIRED},
        {.mask = MIS_PRED, .value = 0, .event = archx::PMU::BR_PRED_RETIRED},
        {.mask = MIS_PRED, .value = MIS_PRED, .event = archx::PMU::BR_MIS_PRED_RETIRED},
        {.mask = MIS_PRED | TAKEN, .value = MIS_PRED | TAKEN, .event = archx::PMU::BR_TAKEN_MIS_PRED_RETIRED},
        {.mask = MIS_PRED | TAKEN, .value = MIS_PRED, .event = archx::PMU::BR_SKIP_MIS_PRED_RETIRED},
        {.mask = IND, .value = IND, .event = archx::PMU::BR_IND_RETIRED},
        {.mask = IND | RETURN | TAKEN, .value = IND | TAKEN, .event = archx::PMU::BR_INDNR_TAKEN_RETIRED},
        {.mask = IND | MIS_PRED, .value = IND | MIS_PRED, .event = archx::PMU::BR_IND_MIS_PRED_RETIRED},
        {.mask = IND | RETURN | MIS_PRED,
         .value = IND | RETURN | MIS_PRED,
         .event = archx::PMU::BR_RETURN_MIS_PRED_RETIRED},
        {.mask = IND | RETURN | MIS_PRED, .value = IND | MIS_PRED, .event = archx::PMU::BR_INDNR_MIS_PRED_RETIRED},
        {.mask = IND | MIS_PRED, .value = IND, .event = archx::PMU::BR_IND_PRED_RETIRED},
        {.mask = IND | RETURN | MIS_PRED, .value = IND | RETURN, .event = archx::PMU::BR_RETURN_PRED_RETIRED},
        {.mask = IND | RETURN | MIS_PRED, .value = IND, .event = archx::PMU::BR_INDNR_PRED_RETIRED},
        {.mask = IND, .value = 0, .event = archx::PMU::BR_IMMED_RETIRED},
        {.mask = IND | TAKEN, .value = TAKEN, .event = archx::PMU::BR_IMMED_TAKEN_RETIRED},
        {.mask = IND | MIS_PRED, .value = MIS_PRED, .event = archx::PMU::BR_IMMED_MIS_PRED_RETIRED},
        {.mask = IND | MIS_PRED, .value = 0, .event = archx::PMU::BR_IMMED_PRED_RETIRED},
        {.mask = RETURN, .value = RETURN, .event = archx::PMU::BR_RETURN_RETIRED},
        {.mask = COND, .value = 0, .event = archx::PMU::BR_UNCOND_RETIRED},
        {.mask = COND, .value = COND, .event = archx::PMU::BR_COND_RETIRED},
        {.mask = COND | TAKEN, .value = COND | TAKEN, .event = archx::PMU::BR_COND_TAKEN_RETIRED}};

    auto branch_type = instruction->archx_instruction->branch_type();

    int scenario = 0;

    if (instruction->is_taken_branch())
        scenario += TAKEN;

    if (instruction->is_mispredicted_direction() || instruction->is_mispredicted_target()
        || instruction->is_mispredicted_branch_type())
    {
        scenario += MIS_PRED;
    }

    if (is_indirect(branch_type))
        scenario += IND;

    if (is_return(branch_type))
        scenario += RETURN;

    if (is_conditional(branch_type))
        scenario += COND;

    for (auto& event : events) {
        if ((scenario & event.mask) == event.value)
            ev_pmu(instruction, ev_num(event.event));
    }
}

void GPC::instruction_commit_checker(const Instruction& instruction)
{
    DBGFLSH("Committing id: {} txid: {}  pc: {} instr: {}",
            instruction->inum().id(),
            instruction->txid().value,
            instruction->pc(),
            instruction->archx_instruction->disasm());
    auto last_committed_instr_id = last_committed_instr ? last_committed_instr->archx_instruction->inum().id() : 0;
    if (instruction->archx_instruction->inum().id() != last_committed_instr_id + 1) {
        std::string msg(
            fmt::format("Instruction committed out of order: expected {} got {} (pc {}, phys addr {}); last committed "
                        "was PC {} phys addr {}",
                        last_committed_instr_id + 1,
                        instruction->archx_instruction->inum().id(),
                        instruction->archx_instruction->program_counter(),
                        instruction->archx_instruction->program_counter_pa(),
                        last_committed_instr->archx_instruction->program_counter(),
                        last_committed_instr->archx_instruction->program_counter_pa()));

        ev_instruction_checker_error(instruction, error_msg(msg));
        AMPERE_MODULE_THROW(msg);
    }

    // Only check translations if warmup is complete because we have boundary
    // condition problems with the global state on the first instruction, and
    // checking for valid translations while we're measuring performance is
    // adequate
    if (instrs_retired && !disable_translation_checker
        && instruction->phys_addr != instruction->archx_instruction->program_counter_pa() + phyaddroffset)
    {
        std::string msg(
            fmt::format("Instruction committed with unexpected"
                        " phys_addr for program_counter {:#x} (inum {})\n"
                        " expected: {:#x}\n"
                        " modeled:  {:#x}\n",
                        instruction->archx_instruction->program_counter().value(),
                        instruction->archx_instruction->inum().id(),
                        instruction->archx_instruction->program_counter_pa().value() + phyaddroffset,
                        instruction->phys_addr.value()));
        msg += "\n"
               + archx::debug_translation(
                   driver_port, archx::pc2va(instruction->archx_instruction->program_counter()), true /*instruction*/);
        AMPERE_ERROR(fmt::runtime(msg));
    }

    last_committed_instr = instruction;
}

Uop GPC::process_mispredicts()
{
    Uop oldest_branch;

    for (auto& port : mispredict_port) {
        while (port->can_take()) {
            auto uop = port->take();
            if (!oldest_branch || uop->is_older(oldest_branch))
                oldest_branch = uop;
        }
    }

    for (auto& port : mispredict_indirect_port) {
        while (port->can_take()) {
            auto uop = port->take();
            if (!oldest_branch || uop->is_older(oldest_branch))
                oldest_branch = uop;
        }
    }
    return oldest_branch;
}

Uop GPC::process_raw_scrubs()
{
    Uop oldest_load;

    while (raw_scrub_port->can_take()) {
        auto uop = raw_scrub_port->take();
        if (!oldest_load || uop->is_older(oldest_load))
            oldest_load = uop;
    }
    return oldest_load;
}

std::pair<Uop, FlushType> GPC::select_older_flusher(const Uop& mispredict, const Uop& raw_scrub)
{
    if (mispredict && raw_scrub) {
        if (mispredict->is_older(raw_scrub))
            return std::make_pair(mispredict, FlushType::BR_MISPREDICT);
        else
            return std::make_pair(raw_scrub, FlushType::RAW_SCRUB);
    } else if (mispredict)
        return std::make_pair(mispredict, FlushType::BR_MISPREDICT);
    else
        return std::make_pair(raw_scrub, FlushType::RAW_SCRUB);
}

void GPC::generate_flush(const Uop& uop, archx::FlushType type, bool pre_commit, bool post_commit)
{
    // First verify that the flushing uop is actually in the ROB. This covers a race where a flush
    // is already on its way to GPC, and then an older flush removes that uop. This could cause us
    // to incorrectly restart at the point of the younger flush.
    //
    // This only applies to speculative flushes -- for post-commit flushes like ERET and SVC instructions,
    // the uop is already committed before the flush. The check doesn't apply for pre-commit flushes since
    // these are signalled by the IDR before dispatching the uop.
    if (!post_commit && !pre_commit) {
        bool flushing_uop_in_rob{false};
        for (auto& ru : rob) {
            if (ru->get_alloc_id() == uop->get_alloc_id()) {
                flushing_uop_in_rob = true;
                break;
            }
        }

        if (!flushing_uop_in_rob)
            return;
    }

    // On a no-restart flush (used for SWOB drain) we start a timer to block commit for the SWOB drain time. This
    // doesn't satisfy a flush that was requested by IDR, which still needs to be done after the SWOB drain.
    if (type == FlushType::NO_RESTART) {
        AMPERE_ASSERT(uop->swob_occupancy, "Should only do a no-restart flush when SWOB has something in it");
        swob_drain_timer = swob_drain_delay + uop->swob_occupancy;

        // This flush will drain the SWOB (without committing anything), so reset this to avoid an infinite loop
        uop->swob_occupancy = 0;
    } else {
        idr_requested_flush = false;
    }

    auto flush = make_child<Flush>(uop, uop, type);

    // Fusions which attach an unconditional branch or nop to the previous instruction should restart at the
    // first instruction. Other fusions like op-jcc should do the default of restarting at the second instruction.
    if (pre_commit
        || (uop->instructions.size() == 2
            && (uop->instructions[1]->archx_instruction->branch_type() == archx::BranchType::UNCONDITIONAL_DIRECT
                || uop->instructions[1]->archx_instruction->is_nop())))
    {
        flush->pc = uop->instructions[0]->archx_instruction->program_counter();
        flush->flushing_pc = uop->instructions[0]->archx_instruction->program_counter();
        flush->instruction = uop->instructions[0];
        flush->fetch = uop->instructions[0]->fetch;
    }

    ev_flush(uop, uop->pc(), flush_restart_pc_data(flush->pc.value()), flush_type(type));
    DBGFLSH("Flushing iid: {}, pc: {}, disasm: {}", uop->inum().id(), uop->pc(), uop->a64_disasm());
    flush_port->put(flush);

    if (type == FlushType::BR_MISPREDICT)
        ev_pmu(uop, ev_num(archx::PMU::BR_MIS_PRED));
}

void GPC::flush(std::uint64_t flush_alloc_id)
{
    while (rob.size()) {
        auto& uop = rob.back();
        if (uop->is_younger(flush_alloc_id)) {
            flush_uop(uop);
            rob.pop_back();
        } else {
            break;
        }
    }
    dispatch_port->clear();
    flush_req_from_idr->clear();
}

void GPC::flush_uop(const Uop& uop)
{
    DBGFLSH("GPC flushing uop. txid: {} allocid: {}", uop->get_txid(), uop->get_alloc_id());
    if (uop->robid)
        release_credit(uop->robid);
    if (uop->lobid)
        release_credit(uop->lobid);
    if (uop->lubid)
        release_credit(uop->lubid);
    if (uop->sobid)
        release_credit(uop->sobid);
    if (uop->is_gather()) {
        GatherProxyCreditPtr gprox = uop->get_gather_proxy();
        AMPERE_ASSERT(gprox, "Gather must have GatherProxyCreditPtr");
        DBG_GATHERC("GPC flush gather uop. parent txid: {}, txid: {}, alloc_id: {}",
                    uop->get_txid_parent(),
                    uop->get_txid(),
                    uop->get_alloc_id());
        gprox->flush();
    } else if (uop->prfid) {
        release_credit(uop->prfid);
    }
    if (uop->prfid2)
        release_credit(uop->prfid2);
    if (uop->flagsid)
        release_credit(uop->flagsid);
}

void GPC::check_for_braindeath()
{
    if (stash_only)
        return;

    if (get_cycle() - last_commit_cycle > brain_dead_limit) {
        if (last_committed_uop)
            ev_brain_dead(last_committed_uop);
        dump(std::cout);
        AMPERE_ERROR(
            "Simulation brain dead. Last committed instruction ID was "
            "{}. Last uop was committed in cycle {}. Committed {} "
            "warmup instructions and {} measurement instructions so far.",
            last_committed_instr ? last_committed_instr->archx_instruction->inum().id() : 0,
            last_commit_cycle,
            warmup_instrs_retired ? *warmup_instrs_retired : 0,
            instrs_retired ? *instrs_retired : 0);
    }
}

void GPC::start_measurement(const Instruction& at_instruction)
{
    if (start_measurement_cycle)
        return;

    AMPERE_INFO("Start performance measurement after {} instructions @ {} cycles, {}",
                *warmup_instrs_retired,
                get_cycle(),
                sc_core::sc_time_stamp());

    get_environment()->event_stream->enable();
    ev_start_measurement(cycle_event_data(get_cycle()));
    instrs_retired = at_instruction ? 1 : 0;
    uops_committed = 0;
    if (at_instruction) {
        instrs_retired_el[at_instruction->exception_level()] = 1;
        cycles_el[at_instruction->exception_level()] = 1;
    }
    for (int i = archx::EL::EL0; i <= archx::EL::EL3; i++) {
        last_heartbeat_cycles_el[i].reset();
        last_heartbeat_instrs_retired_el[i].reset();
        last_heartbeat_uops_retired_el[i].reset();
    }
    start_measurement_cycle = get_cycle();
    last_heartbeat_cycle = get_cycle();
    cpu_time_start[1] = std::clock();
    wall_time_start[1] = std::chrono::steady_clock::now();
    vmsize_warmup = get_vmsize() / (float)1024 / (float)1024;
}

void GPC::stop_measurement(Event* stop_event)
{
    if (stopped)
        return;

    (*(stop_event ? stop_event : &ev_stop_measurement))(cycle_event_data(get_cycle()));

    sc_core::sc_stop();
    stopped = true;
}

void GPC::simulation_control(const Instruction& instruction, bool exclude_start)
{
    if (stash_only)
        return;

    auto control = *instruction->archx_instruction->control();
    switch (control) {
    case archx::SimulationControl::START_MEASUREMENT:
        if (!exclude_start)
            start_measurement(instruction);
        break;
    case archx::SimulationControl::STOP_MEASUREMENT:
        if (!stopped) {
            if (start_measurement_cycle) {
                AMPERE_INFO("Stop performance measurement after {} instructions @ {}",
                            *instrs_retired,
                            sc_core::sc_time_stamp());
            } else {
                AMPERE_INFO("Stop warmup after {} instructions @ {}", *warmup_instrs_retired, sc_core::sc_time_stamp());
            }
        }
        stop_measurement();
        break;
    default:
        break;
    }
}

void GPC::print_simulator_metrics(
    const char* phase, uint64_t cycles, uint64_t instructions, float khz, float kips, float vmsize)
{
    auto ipc = instructions / (float)cycles;
    fmt::print(std::cout,
               " Simulator {:<11} : {:>8} cycles, {:>8} instructions, {:>6.3f} IPC, {:>6.2f} kHz, {:>6.2f} kips, "
               "{:>6.2f} MB virtmem\n",
               phase,
               fmt::group_digits(cycles),
               fmt::group_digits(instructions),
               ipc,
               khz,
               kips,
               vmsize);

    std::string str = "warmup";
    const auto& el_retired = str == phase ? warmup_instrs_retired_el : instrs_retired_el;
    const auto& el_cycles = str == phase ? warmup_cycles_el : cycles_el;
    for (int i = archx::EL::EL0; i <= archx::EL::EL3; i++) {
        if (!el_retired[i].has_value())
            continue;
        fmt::print(std::cout,
                   "  EL{:<19}: {:>8} cycles, {:>8} instructions, {:>6.3f} IPC\n",
                   i,
                   fmt::group_digits(el_cycles[i].value_or(0)),
                   fmt::group_digits(el_retired[i].value_or(0)),
                   el_retired[i].value_or(0) / (float)el_cycles[i].value_or(1));
    }
}

/*************************************************************************
 * end of simulation, print out final stat
 *
 *************************************************************************/
void GPC::end_of_simulation()
{
    Module::end_of_simulation();
    ev_emulation(emulation_divergences(driver_port->validation("emulation divergences")));
    if (!start_measurement_cycle && !stash_only) {
        AMPERE_WARNING("No simulation cycles in measurement region. Ended after {} cycles, {} warmup instructions.",
                       get_cycle(),
                       warmup_instrs_retired.value_or(0));
    }

    float khz[2], kips[2];
    unsigned cpu_msec[2], cpu_sec;
    unsigned wall_msec[2], wall_sec;
    const std::clock_t cpu_time_end = std::clock();
    const std::chrono::steady_clock::time_point wall_time_end = std::chrono::steady_clock::now();
    auto cycles = get_cycle() - start_measurement_cycle.value_or(0);
    float vmsize = get_vmsize() / (float)1024 / (float)1024;

    // calculate speed of warmup region
    wall_msec[0] = std::chrono::duration_cast<std::chrono::milliseconds>(
                       (start_measurement_cycle ? wall_time_start[1] : wall_time_end) - wall_time_start[0])
                       .count();
    cpu_msec[0] =
        1000.0 * ((start_measurement_cycle ? cpu_time_start[1] : cpu_time_end) - cpu_time_start[0]) / CLOCKS_PER_SEC;
    // using cpu time for simulation speeds
    kips[0] = warmup_instrs_retired.value_or(0) / (double)cpu_msec[0];
    khz[0] = start_measurement_cycle.value_or(get_cycle()) / (double)cpu_msec[0];

    cpu_sec = (cpu_time_end - cpu_time_start[0]) / CLOCKS_PER_SEC;
    wall_sec = std::chrono::duration_cast<std::chrono::seconds>(wall_time_end - wall_time_start[0]).count();

    fmt::print(std::cout,
               "\nPanthera completed     : {:>8} cycles, {:>8} instructions total, in {} cpu sec ({} wall sec)\n",
               fmt::group_digits(get_cycle()),
               fmt::group_digits(warmup_instrs_retired.value_or(0) + instrs_retired.value_or(0)),
               cpu_sec,
               wall_sec);

    print_simulator_metrics("warmup",
                            start_measurement_cycle.value_or(get_cycle()),
                            warmup_instrs_retired.value_or(0),
                            khz[0],
                            kips[0],
                            vmsize_warmup);

    if (start_measurement_cycle) {
        // calculate speed of measurement region
        cpu_msec[1] = 1000.0 * (cpu_time_end - cpu_time_start[1]) / CLOCKS_PER_SEC;
        wall_msec[1] =
            std::chrono::duration_cast<std::chrono::milliseconds>(wall_time_end - wall_time_start[1]).count();
        kips[1] = *instrs_retired / (double)cpu_msec[1];
        khz[1] = cycles / (double)cpu_msec[1];

        print_simulator_metrics("measurement", cycles, *instrs_retired, khz[1], kips[1], vmsize);

        ev_sim_speed(speed_hz_data(khz[1] * 1000),
                     speed_ips_data(kips[1] * 1000),
                     speed_hz_data_warmup(khz[0] * 1000),
                     speed_ips_data_warmup(kips[0] * 1000),
                     ev_cpu_msec_total(1000 * cpu_sec),
                     ev_cpu_msec_warmup(cpu_msec[0]),
                     ev_cpu_msec_measurement(cpu_msec[1]),
                     ev_wall_msec_total(1000 * wall_sec),
                     ev_wall_msec_warmup(wall_msec[0]),
                     ev_wall_msec_measurement(wall_msec[1]));
    }

    std::cout << "\n\n";
}

void GPC::dump(std::ostream& os) const
{
    os << "====================== " << name() << " =================================\n";
    fmt::print(os,
               "{:<10} {:<10} {:<9} {:<12} {:>12} {:>20} {:>8} {:>8} {:>8}\n",
               "txid",
               "alloc_id",
               "complete",
               "PC (VA)",
               "MemOp's VA",
               "mnemonic",
               "cleave1",
               "cleave2",
               "wrpath");
    for (auto& uop : rob) {
        fmt::print(os,
                   "{:<10} {:<10} {:<9} {:<12} {:>12} {:>20} {:>8} {:>8}, {:>8}\n",
                   uop->txid().value,
                   uop->get_alloc_id(),
                   uop->complete ? "C" : " ",
                   uop->pc(),
                   uop->virt_addr ? *uop->virt_addr : VirtAddr(0),
                   uop->archx_uop->opcode,
                   uop->cleaved_first_half,
                   uop->cleaved_second_half,
                   uop->wrong_path());
    }
    os << "============================================================" << std::endl;
    os.flush();
}

// print out heartbeat, and IPC of each region
void GPC::print_heartbeat(const std::string& phase, std::uint64_t instrs)
{
    Cycle curr = get_cycle();

    const auto& el_retired = phase == "Warmup  " ? warmup_instrs_retired_el : instrs_retired_el;
    const auto& el_uop_retired = phase == "Warmup  " ? warmup_uops_retired_el : uops_retired_el;
    const auto& el_cycles = phase == "Warmup  " ? warmup_cycles_el : cycles_el;

    // if heartbeat_per_el is on, only print the overall heartbeat in case of instructions in EL1
    // otherwise, allways print the overall heartbeat
    if (heartbeat_print_total
        && (!heartbeat_per_el
            || (heartbeat_per_el
                && (el_retired[archx::EL::EL1].value_or(0)
                        - last_heartbeat_instrs_retired_el[archx::EL::EL1].value_or(0)
                    != 0))))
    {
        fmt::print(std::cout,
                   "Heartbeat: {} {:>14} cycles, {:>11} instructions, {:>11} uops, IPC = {:.2f}\n",
                   phase,
                   fmt::group_digits(curr - start_measurement_cycle.value_or(0)),
                   fmt::group_digits(instrs),
                   fmt::group_digits(uops_committed),
                   heartbeat / (float)(curr - last_heartbeat_cycle));
    }
    if (heartbeat_per_el) {
        std::string align = fmt::format("Heartbeat: {}", phase);

        for (int i = archx::EL::EL0; i <= archx::EL::EL3; i++) {
            if (el_retired[i].value_or(0) - last_heartbeat_instrs_retired_el[i].value_or(0) != 0) {
                auto ipc = (el_retired[i].value_or(0) - last_heartbeat_instrs_retired_el[i].value_or(0))
                           / (float)(el_cycles[i].value_or(1) - last_heartbeat_cycles_el[i].value_or(0));
                fmt::print(std::cout,
                           "{} EL{}: {:>9} cycles, {:>11} instructions, {:>11} uops, IPC = {:.2f}\n",
                           align,
                           i,
                           fmt::group_digits(el_cycles[i].value_or(0)),
                           fmt::group_digits(el_retired[i].value_or(0)),
                           fmt::group_digits(el_uop_retired[i].value_or(0)),
                           ipc);
            }
            last_heartbeat_cycles_el[i] = el_cycles[i];
            last_heartbeat_instrs_retired_el[i] = el_retired[i];
            last_heartbeat_uops_retired_el[i] = el_uop_retired[i];
            align = fmt::format("{:>19}", "");
        }
    }

    std::cout << std::flush;
    last_heartbeat_cycle = curr;
}

// grabs the amount of virtual memory consumed
unsigned long GPC::get_vmsize()
{
    int pid = getpid();
    unsigned long vmsize = 0;
    char buf[512];
    sprintf(buf, "/proc/%d/stat", pid);
    FILE* proc_fp = fopen(buf, "r");
    if (proc_fp) {
        // the 23rd field in /proc/pid/stat contains the vmsize
        if (fscanf(proc_fp,
                   "%*d %*s %*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %*u %*u %*d %*d %*d %*d %*d %*d %*u %lu",
                   &vmsize)
            != 1)
        {
            vmsize = -1u;
        }
        fclose(proc_fp);
    }
    return vmsize;
}

} // namespace ampere::core

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
