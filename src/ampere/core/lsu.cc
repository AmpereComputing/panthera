// Copyright (c) 2018-2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include "ampere/core/lsu.h"
#include "ampere/archx/addr.h"
#include "ampere/archx/bitfield.h"
#include "ampere/archx/el.h"
#include "ampere/archx/flush_type.h"
#include "ampere/archx/sysreg.h"
#include "ampere/archx/translation.h"
#include "ampere/archx/uop_info.h"
#include "ampere/core/lsu/cache_conflicts.h"
#include "ampere/core/lsu/ofb.h"
#include "ampere/core/prefetcher.h"
#include "ampere/archx/page_size.h"
#include "ampere/core/ixu_defs.h"
#include "ampere/core/backend_prefetcher.h"
#include "ampere/core/prefetchers/l1p_oracle.h"
#include "ampere/core/mmu/ack.h"
#include "ampere/util/bits.h"
#include "ampere/sc/report.h"
#include "ampere/sc/runnable.h"
#include "ampere/uarch_common/memop.h"
#include "ampere/uarch_common/memreq.h"
#include "sysc/kernel/sc_simcontext.h"
#include <algorithm>
#include <bit>
#include <boost/range/adaptor/reversed.hpp>
#include <cstdint>
#include <tuple>
#include <type_traits>
#include <iterator>
#include <optional>

// #define DEBUG_LSU
#if defined(DEBUG_LSU)
#define DBG_LSU(format_str, ...) fmt::print(stderr, "{}: " format_str "\n", get_cycle(), ##__VA_ARGS__)
#else
#define DBG_LSU(format_str, ...)
#endif

namespace ampere::core {

LSU::LSU(const sc_core::sc_module_name& name) : Module(name), loads_per_pipe(load_pipes, 0), pob("pob")
{
    connect(dmmu_refill_complete_pipe).name("lsu_to_dmmu_response_complete_pipe");
    connect(dmmu_refill_wakeup_pipe).name("lsu_to_dmmu_wakeup_pipe");

    connect(ld_recycle_complete).name("ld_recycle_complete_pipe").bandwidth(l1d_load_miss_bandwidth());
    connect(ld_fast_recycle_complete).name("ld_fast_recycle_complete_pipe").bandwidth(l1d_load_miss_bandwidth());
    connect(ld_slow_recycle_complete).name("ld_slow_recycle_complete_pipe").bandwidth(l1d_load_miss_bandwidth());
    connect(ld_fill_recycle_complete).name("ld_fill_recycle_complete_pipe").bandwidth(l1d_load_miss_bandwidth());
    connect(ld_split_recycle_complete).name("ld_split_recycle_complete_pipe").bandwidth(load_schedule_bandwidth());
    connect(st_exec_recycle_complete).name("st_exec_recycle_complete_pipe").bandwidth(store_pipes);
    auto st_recycle_complete_pipe =
        connect(st_recycle_complete).name("st_recycle_complete_pipe").bandwidth(store_pipes).bind();
    store_recycle_latency = st_recycle_complete_pipe.get_latency();

    // Latency is determined empirically, to cover the end of the L2 write pipeline before data is returned to LSU
    connect(exclusive_return_delay).name("exclusive_return_delay_pipe");
    connect(stlr_wakeup_delay).name("stlr_wakeup_delay_pipe");

    connect(mlb_entry_release).name("mlb_entry_release_pipe").bandwidth(64);
    connect(mlb_wakeup).name("mlb_wakeup_pipe").bandwidth(64).latency(3);

    auto load_complete_pipe =
        connect(load_complete).name("load_complete_pipe").bandwidth(l1d_load_miss_bandwidth()).bind();

    auto store_complete_pipe = connect(store_complete).name("store_complete_pipe").bandwidth(store_pipes).bind();
    auto store_data_complete_pipe =
        connect(store_data_complete).name("store_data_complete_pipe").bandwidth(store_pipes).bind();
    auto store_data_early_complete_pipe =
        connect(store_data_early_complete).name("store_data_early_complete_pipe").bandwidth(store_pipes).bind();
    AMPERE_ASSERT((!enable_mte && !atomic_load_speculative)
                      || store_complete_pipe.get_latency() >= load_complete_pipe.get_latency(),
                  "Store complete pipe latency must be greater or equal to load pipeline for STA loads");
    AMPERE_ASSERT(store_data_complete_pipe.get_latency() >= store_data_early_complete_pipe.get_latency(),
                  "Early store data completion must be before store data complete");

    connect(store_delayed_completion).name("store_delayed_completion").latency(2).bandwidth(store_pipes * 4);

    connect(stret_wakeup).name("stret_wakeup_pipe").bandwidth(64).latency(2);
    connect(sta_wakeup).name("sta_wakeup_pipe").bandwidth(store_pipes + num_execution_units).latency(2);
    connect(sob_dealloc).name("sob_deallocate_pipe").bandwidth(store_pipes).latency(1);

    auto l2c_refill = connect(l2c_refill_complete_pipe).name("lsu_to_l2c_response_complete_pipe").bind();

    AMPERE_ASSERT(l2c_refill.get_latency() >= load_complete_pipe.get_latency(),
                  "lsu_to_l2c_response_complete_pipe latency smaller than load_complete_pipe latency");
    auto refill_to_load_arbitrate = l2c_refill.get_latency() - load_complete_pipe.get_latency();
    connect(l2c_refill_load_arbitration).name("l2c_refill_load_arbitration").latency(refill_to_load_arbitrate);

    AMPERE_ASSERT(l2c_refill.get_latency() >= store_complete_pipe.get_latency(),
                  "lsu_to_l2c_response_complete_pipe latency smaller than store_complete_pipe latency");
    auto refill_to_store_arbitrate = l2c_refill.get_latency() - store_complete_pipe.get_latency();
    connect(l2c_refill_store_arbitration).name("l2c_refill_store_arbitration").latency(refill_to_store_arbitrate);

    if (l1d_cache_conflicts.evictions_cause_conflicts()) {
        AMPERE_ASSERT(l2c_refill.get_latency() > load_complete_pipe.get_latency(),
                      "must be at least a cycle between refill and load");
        auto refill_to_evict_arbitrate = l2c_refill.get_latency() - load_complete_pipe.get_latency() - 1;
        connect(l2c_refill_evict_arbitration).name("l2c_refill_evict_arbitration").latency(refill_to_evict_arbitrate);
    }

    connect(l2c_refill_wakeup_pipe);

    // Load uops the cycle after l2c refill response (LF03) conflict on L1D PA tag write/read
    connect(l2c_refill_tag_conflict_out, {&l2c_refill_tag_conflict_in})
        .name("l2c_refill_tag_conflict")
        .latency(refill_to_load_arbitrate + 1);

    // Enforce hooks occur before training/trigger (i.e. LSU runs before prefetchers)
    get_clock()->add_zdelay_connection(this, prefetcher.get());
    for (auto* child : prefetcher->get_child_objects()) {
        if (dynamic_cast<Prefetcher<BackendMemReq>*>(child))
            if (auto* child_runnable = dynamic_cast<Runnable*>(child); child_runnable)
                get_clock()->add_zdelay_connection(this, child_runnable);
    }

    connect_from(prefetcher->prefetch_request, prefetch_request).name("prefetch_requests").potentially_lossy();

    raw_scrub_lower_address_bit = compute_num_address_bits(raw_scrub_granularity);

    dl1tlb.driver.bind(driver_port);
    OracleBackendPrefetcher<BackendMemReq>::try_setup(prefetcher.get(), oracle_driver_port);

    if (pwc) {
        connect_from(pwc->store_request, pwc_request).name("store_requests");
        connect_to(pwc_response, pwc->store_response).name("store_responses");
        connect_from(pwc->store_deallocate, pwc_deallocate).name("store_deallocates");
    }

    AMPERE_ASSERT(!(prefetch_uses_store_pipes && !store_pipes_have_tag_ports),
                  "Store pipes must have tag ports to be able to execute prefetches");

    // If port1 loads with fill conflicts are allowed to schedule briefly before the conflict is detected, panthera
    // treats this as if the load has used the pipe for that cycle, preventing a prefetch from scheduling. In RTL,
    // the conflict is detected fast enough that a prefetch can still go. Since this isn't an expected combination
    // of parameters, it's not worth the trouble of fixing.
    if (fast_fill_conflict_port1 && !prefetch_uses_store_pipes)
        AMPERE_ERROR(
            "Modeling is not correct when prefetches use load pipes and port1 loads with fill conflicts are allowed to "
            "schedule");

    // Ensure OFB runs before LSU main loop
    get_clock()->add_zdelay_connection(&ofb, this);
}

bool LSU::run()
{
    gather_wake_sent = 0;
    std::fill(loads_per_pipe.begin(), loads_per_pipe.end(), 0);

    load_pipes_used = 0;
    loads_sched_pipe = 0;
    stores_executed = 0;
    stores_sched_pipe = 0;
    used_dl1_ports = 0;
    mmu_miss_request_channels = 0;

    l1d_cache_conflicts.reset();

    sob_deallocate_stage();
    handle_flush();

    dmmu_refill_acks();
    dmmu_refill_complete();
    dmmu_refill_wakeup();
    dmmu_refill_response();
    if (otb_full_sleep)
        dmmu_wakeup_otb_full();
    tlb_invalidations();
    wakeup_non_speculative();
    release_mlb_entries();
    wakeup_mlb_dependents();
    wakeup_stret_dependents();

    // LF06/LD03 (aligns with load-complete)
    l2c_refill_complete();
    // LF03/LA03
    l2c_refill_response();
    // LF07 (aligns with LD01 of dependencies)
    l2c_refill_wakeup();

    l2c_snoop_request();

    // GC02/LR02
    gpc_store_commit_response();
    gpc_load_commit_response();

    // LD03
    sta_load_complete_stage();

    // LS03
    sta_wakeup_stage();
    // XU05/LW03
    store_data_early_complete_stage();
    store_data_complete_stage();
    // LS04
    store_complete_stage();
    // LR02
    store_retirement_pipe.run();

    // LD03
    load_complete_stage();

    // DR05/XU01
    receive_dispatched_uops();
    // XU04
    receive_issued_uops();

    l2c_refill_tag_conflicts();
    l2c_refill_evict_conflicts();

    if (pwc)
        pwc_schedule();

    // Arbitrate and execute loads/STAs (LA04/LD01/LS01)
    load_store_pipeline();

    // After demand loads are complete, try to issue opportunistic loads
    // LA04/LD01/LS01 for the spawned uop (aligned with load pipeline)
    prefetch_schedule();

    if (early_store_completion_allowed()) {
        store_delayed_completion_stage();
        complete_pending_stores();
    }

    clear_flush();

    send_pmu_state();

    get_barrier_info();

    l2c_refill_evict_schedule();
    // LF05/LD01 (aligns with load-execute)
    l2c_refill_schedule(l2c_refill_load_arbitration);
    // LF05/LS01 (aligns with sta-execute)
    l2c_refill_schedule(l2c_refill_store_arbitration);

    return work_to_do();
}

bool LSU::work_to_do() const
{
    // Panthera perf: we only want to run the LSU next cycle if there might be something useful to do.
    // Note that this impact some PMUs (specifically the STALL* counters) - set run_always if accuracy
    //   of PMU counters is important
    // Everything below here is just deciding whether to return true or false from run()

    // First check whether there are any loads ready to execute.
    for (const auto& [_, lob_entry] : lob) {
        if (lob_entry.is_ready())
            return true;

        // Faulting and STLR-sleep uops get woken up when they are the oldest across both LOB and SOB. That's not
        // really accurate and should probably be improved, but these conditions are rare so for now just run if
        // any uop is in these sleep states.
        auto wake = lob_entry.wake_code;
        if (lob_entry.waiting_till_non_spec() || wake == WakeCode::BASES_LOADED)
            return true;
    }

    // Now check whether there are any stores ready to execute or retire
    unsigned retiring{0};
    for (const auto& [_, sob_entry] : sob) {
        if (sob_entry.is_ready()
            || ((enable_mte || speculative_atomic_load_candidate(sob_entry.uop)) && !sob_entry.sta_load_complete))
            return true;

        // In general a committed store wants to retire, except that a store might hang around waiting to see
        // whether it can combine its retirement with the next store. This can erase all the performance we
        // get from trying not to run the LSU module, so we track whether there is a delayed-retirement store
        // and only ask to run next cycle if there are other stores besides the delayed one ready to retire.
        if (sob_entry.committed)
            if (++retiring > delayed_stores)
                return true;

        // Exclusives are special: they retire when they are marked exclusive_complete. Once exclusive_retire
        // is set they are done with the retirement pipeline, and can commit after the L2 response arrives.
        if (sob_entry.exclusive_is_ready_to_retire())
            return true;

        // Faulting uops get woken up when they are the oldest across both LOB and SOB. That's not really accurate
        // and should probably be improved, but faults are rare so for now just run if any uop is in fault sleep.
        if (sob_entry.waiting_till_non_spec())
            return true;

        if (early_store_completion_allowed() && sta_needs_schedule_for_completion(sob_entry))
            return true;
    }

    // If there are no loads or stores to execute, then the only thing left to do is issue prefetches
    return !pob.empty() || deferred_lob_dealloc || !evictions.empty() || run_always;
}

void LSU::handle_flush()
{
    if (flush->can_read()) {
        auto flush_info = flush->read();

        flush_id = flush_info->alloc_id;
        DBG_LSU("{}: LSU FLUSH alloc_id {}", get_cycle(), *flush_id);

        // Clear LOB/SOB entries
        clear_invalid_entries(*flush_id);
    }
}

void LSU::clear_flush()
{
    flush_id = std::nullopt;
}

// IDR communicates dispatched mem-uops to LSU for oracle usage
void LSU::receive_dispatched_uops()
{
    while (mem_rob_preview_port->can_take()) {
        auto uop = mem_rob_preview_port->take();
        if (!flush_id || !uop->is_younger(*flush_id)) {
            if (uop->is_store_address()) {
                preview_rob.emplace_hint(preview_rob.end(), uop->alloc_id, uop);
            }
        }
    }
}

void LSU::fill_out_sob_entry(SOBEntry& sob_entry)
{
    auto& uop = sob_entry.uop;

    if (uop->is_single_uop_store() && uop->sus.sta_issued && uop->sus.std_issued && !uop->sus.split)
        sob_entry.fat_sus = true;
    sob_entry.split = enable_splits && uop->is_line_split();
    sob_entry.split_4k = enable_splits && uop->is_4k_split();
    sob_entry.issue_cycle = get_cycle();

    // If the SOB contains anything which will wait for the PWC to be empty, or wait for a specific
    // PWC entry to complete, then send a signal to the L2C to tell the PWC to drain immediately.
    // These include:
    // - full barriers (DSB, DMB-all) which wait for PWC (and all fill buffers) to be empty
    // - partial barriers (DMB-store) which cannot schedule out of PWC until all older stores are complete
    // - store-release, which blocks younger load-acquires until it completes
    // - store-exclusive, which returns a pass/fail value only after its store is complete
    // - atomics, which behave like ordered stores
    if (uop->is_store_barrier() || uop->is_release() || uop->is_exclusive() || uop->is_atomic()) {
        sob_entry.barrier = true;
        if (++sob_barrier_count == 1)
            lsu_has_barrier_out->put(true);

        AMPERE_ASSERT((uint64_t)sob_barrier_count <= sob.size(), "SOB barrier count is larger than SOB size!");
    }
}

void LSU::fill_out_sob_entry_and_wake_dependents(SOBEntry& sob_entry)
{
    fill_out_sob_entry(sob_entry);

    // Wake up any loads sleeping due to unknown virtual address
    sta_wakeup.put({sob_entry.uop->alloc_id, false});

    try_completing_sta_at_issue(sob_entry);
}

void LSU::receive_issued_uops()
{
    // LSU takes from multiple AGUs, alternating ld/st
    for (auto& issue : issue_mem) {
        while (issue->can_take()) {
            auto uop = issue->take();

            // Avoid training or doing anything really with unemulated wrong path uops.
            // They'll consume lob/sob credits in the IDR but they actually don't need
            // to occupy the LSU's version of the lob/sob structures
            if (ignore_unemulated_wrong_path_uops && uop->ainst()->wrong_path() && uop->ainst()->emulated() == 0) {
                continue;
            }

            // If flush and IXU uop is older or equal to flush_id, LSU must
            // still execute it

            if (uop->is_load()) {
                // MEM_ACCESS is computed in rollup_formula.py as MEM_ACCESS_RD + MEM_ACCESS_WR
                pmu_event(uop, pmu_num_data(archx::PMU::MEM_ACCESS_RD));

                if (!flush_id || !uop->is_younger(*flush_id)) {
                    lob_write_event(uop,
                                    evd_lobid(uop->lob_id_val),
                                    evd_lob_entry_inum(uop->instructions.front()->archx_instruction->inum()));
                    auto it = lob.emplace_hint(lob.end(), uop->alloc_id, uop);

                    if (uop->lubid)
                        lub.emplace_hint(lub.end(), uop->alloc_id, uop);

                    // Record the uop's original datasize in case we need to modify it for a split
                    it->second.mem_datasize = uop->mem_datasize();
                    it->second.split = enable_splits && uop->is_line_split();
                    it->second.split_4k = enable_splits && uop->is_4k_split();
                    it->second.issue_cycle = get_cycle();

                    if (!(enable_nontemporals && uop->is_nontemporal()))
                        prefetcher->issue(uop->alloc_id, {uop});
                } else { // Drop load from IXU and return LOB credit
                    credit_return->put(std::move(uop->lobid));
                    credit_return->put(std::move(uop->lubid));
                }
            } else if (uop->is_store()) {
                if (!flush_id || !uop->is_younger(*flush_id)) {
                    auto sob_iter = sob.find(uop->store_associated_sta_id);
                    bool is_plain_sta = uop->is_store_address() && !uop->is_single_uop_store();
                    bool is_sus_addr_only = uop->is_single_uop_store() && uop->sus.sta_issued && !uop->sus.std_issued;
                    bool is_sus_single_fire =
                        uop->is_single_uop_store() && uop->sus.sta_issued && uop->sus.std_issued && !uop->sus.split;

                    if (sob_iter == sob.end()) { // First uop for store, write SOB entry
                        sob_write_event(uop);

                        bool store_addr =
                            is_plain_sta || is_sus_addr_only || is_sus_single_fire || uop->is_store_special();

                        if (store_addr) {
                            auto it = sob.emplace_hint(sob.end(), uop->store_associated_sta_id, uop);
                            if (uop->is_sw_prefetch_as_sta()) {
                                // Non-STA dispatching on store pipe (e.g. SW prefetch)
                                fill_out_sob_entry(it->second);
                            } else {
                                fill_out_sob_entry_and_wake_dependents(it->second);
                            }

                            DBG_LSU("{}: SOB 1st install id {}, txid: {} pc: {:016x} {}",
                                    get_cycle(),
                                    uop->store_associated_sta_id,
                                    uop->get_txid(),
                                    uop->pc().value(),
                                    uop->get_uopcode());
                        } else {
                            // This is a store data uop which arrived before its associated STA
                            sob.emplace_hint(sob.end(), uop->store_associated_sta_id, Uop());
                            store_data_execute(uop);
                        }
                        DBG_LSU("write in sob pc 0x{:x} {}, with sta {}",
                                uop->pc().value(),
                                uop->get_uopcode(),
                                uop->store_associated_sta_id);
                    } else { // Second uop for store, update SOB entry
                        sob_update_event(uop);

                        auto& sob_entry = sob_iter->second;
                        if (uop->is_super_sus() && uop->is_pair() && !uop->sus.std_issued) {
                            // Single-uop store pairs can issue up to 3 times (address, data1, data2). If we get here,
                            // this is the second issue, but only half the data is ready, so don't do anything.
                        } else if (is_plain_sta || is_sus_addr_only || is_sus_single_fire) {
                            // This is the address for the store. The is_sus_single_fire case can be true for a
                            // store-pair, which has a SUS followed by an extra store-data uop.
                            sob_entry.uop = uop;

                            DBG_LSU("{}: SOB 2nd install id {}, txid: {} pc: {:016x} {}",
                                    get_cycle(),
                                    uop->store_associated_sta_id,
                                    uop->get_txid(),
                                    uop->pc().value(),
                                    uop->get_uopcode());

                            fill_out_sob_entry_and_wake_dependents(sob_entry);
                        } else {
                            // This is the data for the store
                            store_data_execute(uop);
                        }
                    }
                } else // Drop store from IXU and return SOB credit
                    credit_return->put(std::move(uop->sobid));
            } else
                AMPERE_ERROR("Uop issued to LSU not a mem Uop");

            // Ensure Uop has an associated virtual address
            if (!uop->virt_addr) {
                AMPERE_ERROR("No VirtAddr for Uop issued to LSU");
            }
        }
    }
}

void LSU::send_pmu_state()
{
    // If L2C reports a load miss, mark all LOB entries matching that cacheline
    // as having missed the L2C
    if (pmu_l2_miss_in->can_read()) {
        auto l2_miss_phys_addr = pmu_l2_miss_in->read();

        for (auto& [_, lob_entry] : lob) {
            if (lob_entry.uop->phys_addr && l2_miss_phys_addr.same_cache_line(*lob_entry.uop->phys_addr))
                lob_entry.l2_miss = true;
        }

        ofb.mark_l2_miss(l2_miss_phys_addr);
    }

    bool ld_next_to_commit = false;
    bool oldest_load_atomic = false;
    if (auto oldest_lob = lob.begin(); oldest_lob != lob.end()) {
        auto& lob_entry = oldest_lob->second;
        if (is_non_speculative(lob_entry))
            ld_next_to_commit = true;

        // Send state of oldest load this cycle for PMU event determination at IDR
        if (ld_next_to_commit && !lob_entry.complete && lob_entry.wake_code) {
            if (lob_entry.wake_code == WakeCode::TLB_MISS || lob_entry.wake_code == WakeCode::OTB_ACK)
                pmu_oldest_load_tlb_out->put(true);
            else if (lob_entry.wake_code == WakeCode::L1_MISS || lob_entry.wake_code == WakeCode::OFB_FULL) {
                if (lob_entry.wake_code == WakeCode::OFB_FULL)
                    pmu_oldest_load_ofb_full_out->put(true);

                if (lob_entry.l2_miss)
                    pmu_oldest_load_mem_out->put(true);
                else
                    pmu_oldest_load_cache_out->put(true);
            } else if (lob_entry.wake_code == WakeCode::ST_ADDR || lob_entry.wake_code == WakeCode::ST_DATA
                       || lob_entry.wake_code == WakeCode::ST_RETIRE)
                pmu_oldest_load_st_wait->put(true);
        }

        if (ld_next_to_commit && lob_entry.uop->is_atomic())
            oldest_load_atomic = true;
    }

    bool st_next_to_commit = false;
    bool oldest_store_atomic = false;
    if (auto oldest_sob = sob.begin(); oldest_sob != sob.end()) {
        auto& sob_entry = oldest_sob->second;
        auto& uop = oldest_sob->second.uop;

        if (uop) {
            if (is_non_speculative(sob_entry))
                st_next_to_commit = true;
            if (st_next_to_commit && uop->is_atomic())
                oldest_store_atomic = true;
        }
    }

    if (ld_next_to_commit || st_next_to_commit)
        pmu_oldest_ldst_valid_out->put(true);

    if (oldest_load_atomic || oldest_store_atomic)
        pmu_oldest_ldst_atomic_out->put(true);

    if (enable_per_cycle_pmu) {
        for (const auto& [_, lob_entry] : lob) {
            if (!lob_entry.complete) {
                // MEM_ACCESS_PERCYC is computed in rollup_formula.py as MEM_ACCESS_RD_PERCYC + MEM_ACCESS_WR_PERCYC
                pmu_event(lob_entry.uop, pmu_num_data(archx::PMU::MEM_ACCESS_RD_PERCYC));
            }
        }

        for (const auto& [_, sob_entry] : sob) {
            if (!sob_entry.is_execution_complete()) {
                // MEM_ACCESS_PERCYC is computed in rollup_formula.py as MEM_ACCESS_RD_PERCYC + MEM_ACCESS_WR_PERCYC
                pmu_event(sob_entry.uop, pmu_num_data(archx::PMU::MEM_ACCESS_WR_PERCYC));
            }
        }
    }
}

void LSU::get_barrier_info()
{
    // Get the signal from the L2 that says the PWC and fill buffers are
    // empty of writes and so it's OK to commit a barrier.
    if (l2_no_pending_stores_in->can_take()) {
        can_commit_barrier = l2_no_pending_stores_in->take();

        // After the L2 says all stores are done, add some delay to make the pipeline match RTL
        if (can_commit_barrier)
            barrier_can_execute_cycle = get_cycle() + barrier_wakeup_delay;
    }

    if (l2_barrier_in->can_take())
        l2_pending_barrier = l2_barrier_in->take();

    // We get a separate signal from L2 saying whether there is a pending store-release. The rising edge of that
    // signal takes effect right away, so that we don't let any LDARs slip through without noticing the older STLR.
    // On the falling edge we add some delay to match the RTL latency.
    if (l2_store_release_in->can_take()) {
        auto incoming_pending_stlr = l2_store_release_in->take();

        if (incoming_pending_stlr)
            l2_pending_store_release = true;
        else
            stlr_wakeup_delay.put(incoming_pending_stlr);
    }

    // On the falling edge of pending_store_release (delayed to match RTL), we wake up any loads which are in STLR sleep
    if (stlr_wakeup_delay.can_take()) {
        auto prev_l2_pending_store_release = l2_pending_store_release;
        l2_pending_store_release = stlr_wakeup_delay.take();

        if (prev_l2_pending_store_release && !l2_pending_store_release && !retire_pending_store_releases
            && (!pwc || !pwc->pending_store_release()))
        {
            for (auto& [_, lob_entry] : lob) {
                if (lob_entry.wake_code == WakeCode::STLR) {
                    lob_entry.wakeup();
                    uop_ld_stlr_wakeup_event(lob_entry.uop);
                }
            }
            if (atomic_load_speculative && atomic_load_speculative_abort_matches)
                wakeup_by_code(WakeCode::STLR, sob);
        }
    }

    // When L2 goes from having an exclusive to not having one, we send the store at the head of the SOB (which
    // had better be an exclusive) back through the pipeline to write back.
    //
    // This signal arrives from the L2 aligned with pending_store_release, so that for an exclusive-release (or atomic)
    // they both come at the same time. This lets us set the exclusive_second_pass flag on the SOB entry, so that any
    // load-acquires which were woken by the falling edge of l2_pending_store_release know not to go back to sleep.
    // However the data doesn't actually arrive for a few more cycles, which we model with exclusive_return_delay.
    if (l2_store_exclusive_in->can_take()) {
        auto new_l2_pending_store_excl = l2_store_exclusive_in->take();
        if (real_exclusives && l2_pending_store_excl && !new_l2_pending_store_excl && !sob.empty()) {
            auto& sob_entry = sob.begin()->second;
            AMPERE_ASSERT_ERROR(sob_entry.uop->is_exclusive() || sob_entry.uop->is_atomic_load(),
                                "L2 sent exclusive completion but the head of the SOB is not an exclusive/atomic");

            if (!sob_entry.uop->is_atomic_load()) {
                sob_entry.exclusive_receives_data();
                exclusive_return_delay.put(sob_entry.uop->alloc_id);
                uop_st_exclusive_release_event(sob_entry.uop);

                if (sob_entry.wrote_back)
                    uop_st_speculative_exclusive_event(sob_entry.uop);
            }
        }
        l2_pending_store_excl = new_l2_pending_store_excl;
    }

    // After a few cycles delay for the data to arrive, mark the exclusive as ready to re-execute (and write back).
    while (exclusive_return_delay.can_take()) {
        auto sob_wake_id = exclusive_return_delay.take();
        auto& sob_entry = sob.begin()->second;
        AMPERE_ASSERT(sob_entry.uop->alloc_id == sob_wake_id, "Trying to wake an exclusive but alloc ids don't match");
        sob_entry.must_recycle = true;
        sob_entry.executing = false;
    }

    // If a barrier is at the head of the ROB, the GPC will send us its alloc_id
    while (next_to_commit_in->can_take()) {
        non_spec_uop_id = next_to_commit_in->take();
    }
}

bool LSU::can_load_pipeline_schedule() const
{
    auto ports_used = load_pipes_used;
    if (load_fill_conflicts && l2c_refill_load_arbitration.can_take())
        ports_used += l2c_refill_load_arbitration.peek().executed();
    if (l1d_cache_conflicts.evictions_cause_conflicts() && l2c_refill_evict_arbitration.can_take())
        ports_used += l2c_refill_evict_arbitration.peek().executed();

    bool extra_slots_available =
        load_combining && std::any_of(loads_per_pipe.begin(), loads_per_pipe.end(), [&](const auto& loads) {
            return loads < (load_combining.value_or(1) - 1);
        });
    return ports_used < load_pipes || extra_slots_available;
}

bool LSU::can_sta_pipeline_schedule() const
{
    auto ports_used = stores_executed;
    if (sta_fill_conflicts && l2c_refill_store_arbitration.can_take())
        ports_used += l2c_refill_store_arbitration.peek().executed();

    return ports_used < sta_pipes();
}

bool LSU::can_prefetch_schedule() const
{
    return prefetch_uses_store_pipes ? can_sta_pipeline_schedule() : can_load_pipeline_schedule();
}

bool LSU::can_sta_load_schedule(SOBEntry& sob_entry, bool may_writeback) const
{
    if (may_writeback && !sta_load_writeback_can_bypass
        && (get_cycle() - sob_entry.issue_cycle) <= store_recycle_latency)
        return false;

    bool requires_load_pipe = may_writeback || !store_pipes_have_tag_ports;
    if (requires_load_pipe
        && (!can_load_pipeline_schedule() || (loads_sched_pipe & sob_entry.pipe_mask())
            || (load_fill_conflicts && l2c_refill_pipe_consumed(l2c_refill_load_arbitration, sob_entry.pipe_mask()))))
        return false;

    if (!can_sta_schedule(sob_entry))
        return false;

    return true;
}

std::optional<unsigned> LSU::choose_dl1_port(unsigned pipe) const
{
    if (older_load_gets_port0)
        return std::countr_one(used_dl1_ports);

    if (used_dl1_ports & (1ULL << pipe))
        return {};
    return pipe;
}

void LSU::load_pipeline()
{
    // LOB is age ordered
    for (auto& [_, lob_entry] : lob) {
        auto port = choose_dl1_port(lob_entry.uop->sch.lsu_scheduler_pipe());
        if (!port)
            continue;

        lob_entry.dl1_port = *port;
        if (!can_load_schedule(lob_entry))
            continue;

        execute_load(lob_entry);

        if (!can_load_pipeline_schedule())
            break;
    }
}

int LSU::lsu_port_lookup(Uop u, bool as_load) const
{
    auto pipe = u->sch.pipe_type;
    if (as_load)
        pipe = corresponding_load_pipe(pipe);

    return expand_pipe(pipe, u->sch.partition);
}

// Scheduling of LOB/SOB entries is independent (when MTE is disabled) and
// co-dependent (when MTE is enabled). For the latter, whether SOB entry can go
// down load pipe depends on whether there are older LOB entries.
//
// 1. Iterate through SOB entries: a) send SOB entry uop down load pipe or send
//    other older loads down load pipe (this step is omitted for MTE-disabled
//    scenario). b) set aside eligible SOB alloc ids (don't send them down store
//    pipes yet until load pipes have been dealt with)
//
// 2. If, at the end of SOB
//    iteration, there are still load pipes left, schedule more loads (MTE-disabled
//    config always exercises this step).
//
// 3. Send the set-aside store uops down store
//    pipes.
void LSU::load_store_pipeline()
{
    // TLB is blocked due to fill
    if (enable_tlb_fill_conflict && block_on_dl1tlb_refill) {
        block_sta_pipe_dl1tlb_refill_event();
        // sta cannot be executed --> implies that STA-loads can't be scheduled as well
        // just handle load uops
        load_pipeline();
        return;
    }

    // Tag conflict with incoming fill just prevents any loads from scheduling instead of recycling them
    if (fill_conflict_blocks_schedule && l1d_cache_conflicts.load_refill_conflict_check()) {
        sta_pipeline();
        return;
    }

    // Set aside SOB entries of to-be-scheduled store uops.
    // After load pipes have been scheduled, these set-aside uops will be sent down store pipes.
    // Load pipes need to be scheduled first because load pipe uops have higher priority in down-the-pipe
    // matters such as sending request to MMU.
    std::vector<SOBEntry*> scheduled_sob_entries;
    scheduled_sob_entries.reserve(store_pipes);

    for (auto& [_, sob_entry] : sob) {
        if (!can_sta_schedule(sob_entry))
            continue;

        bool sta_load_scheduled = false;
        // If address translation has figured out that page is untagged, no need to perform tag check.
        // Store barriers and second pass of atomic load/exclusive store don't require tag check.
        // Predicated off store uops should not perform tag check.
        // don't schedule STA-load in case the SOB entry is asleep or in the pipeline somewhere
        if ((sta_load_optional(sob_entry) || sta_load_required(sob_entry)) && sob_entry.is_ready_for_load()) {
            // executes all older loads and, if load pipe available, executes STA-load
            sta_load_scheduled = iterate_lob_and_schedule_sta_load(sob_entry);
        }

        bool sta_scheduled = sta_load_scheduled || sob_entry.is_ready();
        if (sta_scheduled) {
            scheduled_sob_entries.push_back(&sob_entry);
            // Both are updated to correctly iterate and pick SOB entries. Will be reset before actual STA execution.
            stores_executed++;
            stores_sched_pipe |= sob_entry.pipe_mask();
        }

        AMPERE_ASSERT(!sta_load_scheduled || sta_scheduled,
                      "SOB entry can't be scheduled to load pipe without being scheduled to the store pipe too");

        if (!can_store_pipeline_schedule())
            break;
    }

    // If free load pipes left, schedule more loads. Either because MTE is not
    // enabled or they were not triggered by MTE tag checks (all stores done).
    if (can_load_pipeline_schedule())
        load_pipeline();

    // send set-aside store uops down store pipes.
    stores_executed = 0; // reset because the store uops will be really executed now.
    stores_sched_pipe = 0;
    for (auto* sob_entry : scheduled_sob_entries)
        execute_sta(*sob_entry);

    AMPERE_ASSERT(stores_executed <= sta_pipes(), "More than {} STAs can't be scheduled per cycle", sta_pipes());
    AMPERE_ASSERT(load_pipes_used <= load_pipes, "More than {} loads can't be scheduled per cycle", load_pipes);
}

/* Execute all loads older than input SOB entry.
 * After that, if there are still load pipes available, schedule STA-load.
 * Inputs:
 *      1) sob_entry: SOB entry whose STA-load needs to be scheduled.
 * Output:
 *      return true if STA-load can be scheduled.
 */
bool LSU::iterate_lob_and_schedule_sta_load(SOBEntry& sob_entry)
{
    for (auto& [_, lob_entry] : lob) {
        auto port = choose_dl1_port(lob_entry.uop->sch.lsu_scheduler_pipe());
        if (!port)
            continue;

        lob_entry.dl1_port = *port;
        if (!can_load_schedule(lob_entry))
            continue;

        // schedule this load if it is older than the input SOB entry
        if (lob_entry.uop->is_older(sob_entry.uop->alloc_id)) {
            if (!can_load_pipeline_schedule())
                return false; // no more load pipes free

            execute_load(lob_entry);
        }
    }

    // All possible older loads have been scheduled.
    // If there is still load pipe available, schedule STA-load.
    bool may_writeback = speculative_atomic_load_allowed(sob_entry) || sob_entry.writeback_only();
    auto port = (may_writeback || !store_pipes_have_tag_ports)
                    ? choose_dl1_port(sob_entry.uop->sch.lsu_scheduler_pipe())
                    : sob_entry.uop->sch.lsu_scheduler_pipe();
    if (!port)
        return false;

    sob_entry.dl1_port = *port;
    if (!can_sta_load_schedule(sob_entry, may_writeback))
        return false;

    execute_sta_load(sob_entry, may_writeback); // schedule STA-load down load pipe
    return true;
}

void LSU::execute_load(LOBEntry& lob_entry)
{
    // ANY CHANGE TO THIS FUNCTION SHOULD BE APPROPRIATELY PORTED TO EXECUTE_STA_LOAD() FOR MTE-ENABLED SCENARIO.

    // Pipeline sequence:
    //   1) Determine if store-to-load forward case - complete if so
    //   2) Determine if load conflict case - recycle if so
    //   3) Lookup DL1_TLB for phys_addr - sleep on MMU response if not hit
    //   4) Lookup L1D cache for data - sleep on L2DC response if not hit
    lob_entry.executing = true;
    lob_entry.skipped_unknown_store_this_pass = false;
    uop_ld_execute_event(lob_entry.uop, load_dl1_port(lob_entry.dl1_port));

    if ((loads_sched_pipe & lob_entry.pipe_mask()) == 0)
        load_pipes_used++;
    else
        uop_ld_combining_event(lob_entry.uop);
    loads_sched_pipe |= lob_entry.pipe_mask();
    load_pipe_occupancy_event(lob_entry.uop,
                              pipe_partition(lob_entry.pipe_mask()),
                              load_dl1_port(lob_entry.dl1_port),
                              pipe_occupant(PipeOccupant::LOAD));
    if (lob_entry.issue_cycle + lob_bypass_delay == get_cycle())
        ev_load_bypass_from_ixu(lob_entry.uop);

    // We executed a load on this port, so any future load should go to the next port
    used_dl1_ports |= 1ULL << lob_entry.dl1_port;

    // We may invalidate any pending PCP prefetches to the same line as an executing load
    if (load_exec_inval_pcp) {
        prefetch_request->clear_if([&](const BackendMemReq& req) {
            if (req.prefetch_type == PrefetchType::PCP && req.virt_addr.same_cache_line(*lob_entry.uop->virt_addr)) {
                ev_load_inval_pcp(lob_entry.uop);
                return true;
            }
            return false;
        });
    }

    if (lob_entry.uop->is_predicated_off()) {
        load_complete.put(lob_entry.uop);
        return;
    }

    // If the TLB is blocked due to a fill, recycle the load
    if (enable_tlb_fill_conflict && block_on_dl1tlb_refill) {
        block_load_pipe_dl1tlb_refill_event();
        recycle_load(lob_entry, RecycleReason::DTLB_CONFLICT);
    } else if (l1d_cache_conflicts.load_refill_conflict_check()) {
        uop_write_read_fill_load_sch_conflict_event(lob_entry.uop);
        recycle_load(lob_entry, RecycleReason::FILL_CONFLICT, true);
    } else if (!dl1_tlb_access(lob_entry)) {
        if (enable_early_prfm_tlb && lob_entry.uop->is_sw_prefetch()) {
            // PRFMs can (if enabled) complete after missing the TLB
            load_complete.put(lob_entry.uop);
        } else {
            recycle_load(lob_entry, RecycleReason::DTLB_MISS);
        }
    } else {
        // Compute conflicts before we add this load's address to the list of addresses scheduled this cycle
        LSUCacheConflicts::Conflicts conflicts;
        if (!lob_entry.uop->is_sw_prefetch()) {
            conflicts = l1d_cache_conflicts.load_cache_conflicts(lob_entry.uop,
                                                                 *lob_entry.uop->virt_addr,
                                                                 *lob_entry.uop->phys_addr,
                                                                 lob_entry.mem_datasize,
                                                                 lob_entry.dl1_port);
        }

        // Store forwarding check comes after TLB lookup and fill conflicts, but before load/store
        // conflicts -- so if a load can complete with data from a store it will not conflict.
        //
        // Return value will be true if the load completed with store data, or recycled, or went to sleep
        // waiting on a store. False means L1 lookup should proceed as usual.
        auto forwarding = load_store_forward(lob_entry);

        // Track in-flight loads which might be missed by a store's RAW scrub check, which requires the store to recycle
        // We only need to know about loads which skipped an unknown store.
        if ((in_flight_raw_scrub_window > 0) && lob_entry.skipped_unknown_store_this_pass)
            loads_in_raw_scrub_window.emplace_back(*lob_entry.uop->virt_addr, get_cycle());

        // Even if forwarding completed, recycled, or slept the load, the cache lookup proceeds in parallel.
        // Note that chunk and bank conflicts are checked after the TLB lookup -- if the load misses the
        // TLB it can still send the miss request to MMU, even if it would have had a chunk conflict
        // if it had hit.
        CacheResult cache_result;
        bool can_lookup_cache = conflicts.allowed_to_lookup(!read_conflict_can_miss, !write_conflict_can_miss);
        if (lob_entry.uop->faulted) {
            // Don't send an L2 request for a faulting load, just fake an L1 hit so we can complete it
            cache_result.hit = true;
        } else if (can_lookup_cache) {
            bool spec_lookup = (forwarding || (lob_entry.skipped_unknown_store_this_pass && !skip_miss_proceeds));
            cache_result = l1d_cache_access(lob_entry, spec_lookup);
        }

        if (forwarding.result == ForwardingResult::PARTIAL && (cache_result || can_lookup_cache)) {
            // This load has completed its walk, and it got a partial data forward from at least one store.
            // The load can only complete if it hits the L1, otherwise we have memory ordering issues.
            // If it misses the L1, then it goes to ST_RETIRE sleep on the youngest store which provided data.
            if (cache_result) {
                forwarding.result = ForwardingResult::NONE;
            } else {
                if (lob_entry.has_mlb) {
                    lob_entry.has_mlb = false;
                    mlb_entry_release.put(lob_entry.uop->alloc_id);
                }

                if (forwarding.matched_store()) {
                    auto& sob_entry = sob.at(forwarding.store_alloc_id);
                    sleep_load_on_store(lob_entry,
                                        sob_entry,
                                        lob_entry.youngest_forwarding_store,
                                        WakeCode::ST_RETIRE,
                                        RecycleReason::PARTIAL_DATA);
                } else {
                    forwarding.result = ForwardingResult::NONE;
                }

                // Reset the store forwarding walk so that when the load wakes up it starts the walk over
                lob_entry.youngest_forwarding_store = 0;
                lob_entry.oldest_skipped_store = ULLONG_MAX;
            }
        }

        if (forwarding.result != ForwardingResult::PARTIAL) {
            cache_result.sleep_if_needed(lob_entry);
            if (cache_result.needs_recycle())
                recycle_load(lob_entry, *cache_result.recycle_reason);
        }

        if (forwarding) {
            // If the stfwd logic completed, recycled, or slept the load then we are done after the L1 access
            if (forwarding.can_complete())
                load_complete.put(lob_entry.uop);
        } else if (!skip_miss_proceeds && lob_entry.skipped_unknown_store_this_pass && !cache_result) {
            // The forwarding logic is higher priority than anything else, so a load which skips a store and then
            // fails to hit in the L1 for any reason will go to ST_ADDR sleep waiting on the store that it
            // wanted to skip.
            sleep_load_on_store(lob_entry, lob_entry.skipped_store_id, WakeCode::ST_ADDR, RecycleReason::SKIP_L1_MISS);
        } else if (conflicts.read && (!read_conflict_can_miss || cache_result)) {
            // Chunk conflict with older load scheduled this cycle. Recycle the younger load.
            uop_read_read_chunk_sch_conflict_event(lob_entry.uop);
            recycle_load(lob_entry, RecycleReason::LD_CONFLICT, true);
        } else if (conflicts.write && (!write_conflict_can_miss || cache_result)) {
            // Chunk & bank conflict with retiring store. Recycle the load.
            uop_write_read_chunk_bank_sch_conflict_event(lob_entry.uop);
            recycle_load(lob_entry, RecycleReason::ST_CONFLICT, true);
        } else if (conflicts.fill && (!write_conflict_can_miss || cache_result)) {
            // Chunk & bank conflict with fill. Recycle the load.
            recycle_load(lob_entry, RecycleReason::FILL_CONFLICT, true);
            uop_write_read_fill_load_sch_conflict_event(lob_entry.uop);
        } else if (conflicts.eviction && (!read_conflict_can_miss || cache_result)) {
            // Chunk & bank conflict with eviction. Recycle the load.
            recycle_load(lob_entry, RecycleReason::EVICT_CONFLICT, true);
        } else if (cache_result) {
            if (!lob_entry.uop->is_sw_prefetch() && !lob_entry.uop->is_exclusive())
                l1d_cache_conflicts.load_cache_read(
                    lob_entry.uop, *lob_entry.uop->virt_addr, lob_entry.mem_datasize, lob_entry.dl1_port, conflicts);
            load_complete.put(lob_entry.uop);
            lob_entry.got_l1_data = true;
        } else if (enable_early_prfm && lob_entry.uop->is_sw_prefetch() && !lob_entry.recycle) {
            // PRFMs just complete after missing the L1, if they are not recycling due to OFB full
            load_complete.put(lob_entry.uop);
        } else if (!lob_entry.recycle) {
            // l1d_cache_access might already have set the load to recycle -- if not, recycle it here so that
            // it can't start executing again until it finishes the pipeline
            recycle_load(lob_entry, RecycleReason::L1_MISS);
        } else if (lob_entry.wake_code) {
            // At this point OFB_FULL should be the only valid reason for recycling
            AMPERE_ASSERT(lob_entry.wake_code == WakeCode::OFB_FULL, "Load executed but did not complete as expected");
        }
    }

    // Note that cancel is only sent for loads and STA loads requiring a writeback
    // For the first half of a split, LSU is smart enough to realize that the load can't possibly
    // complete, and not send a wake to IXU. We support sending cancel on splits just for unit tests.
    if (enable_load_cancel && !lob_entry.suppress_cancel) {
        bool split_first_pass = lob_entry.split && !lob_entry.split_lo_complete;
        bool nonsplit_cancel = lob_entry.recycle && !split_first_pass;
        bool split_cancel = splits_send_cancel && split_first_pass;
        if (nonsplit_cancel || split_cancel)
            load_cancel_out->put(lob_entry.uop);
    }

    if (!lob_entry.recycle)
        measure_load_execute_disorder(lob_entry.uop);
}

void LSU::execute_sta_load(SOBEntry& sob_entry, bool may_writeback)
{
    // StaLds perform store-to-load forwarding from STGs but we skip it now because our traces do not have STGs.
    // StaLds do not access DL1 data array. We don't need to consider LD/ST conflicts.
    bool writeback_only = sob_entry.writeback_only();

    // If the store pipes have their own tag ports, then we just emit one PipeOccupant::STA event in
    // execute_sta() -- we don't need a separate STA_LD event.
    if (may_writeback || !store_pipes_have_tag_ports) {
        load_pipes_used++;
        loads_sched_pipe |= sob_entry.pipe_mask();
        load_pipe_occupancy_event(
            sob_entry.uop, pipe_partition(sob_entry.pipe_mask()), pipe_occupant(PipeOccupant::STA_LD));
        uop_sta_load_execute_event(sob_entry.uop);
    }

    AMPERE_ASSERT(!sob_entry.uop->is_predicated_off(), "A predicated off store uop should not perform tag check!");

    if (enable_tlb_fill_conflict && block_on_dl1tlb_refill) {
        recycle_sta_load(sob_entry, RecycleReason::DTLB_CONFLICT);
        block_load_pipe_dl1tlb_refill_event();
    } else if (l1d_cache_conflicts.load_refill_conflict_check()) {
        recycle_sta_load(sob_entry, RecycleReason::FILL_CONFLICT, true);
        uop_write_read_fill_load_sch_conflict_event(sob_entry.uop);
    } else if (!writeback_only && !dl1_tlb_access(sob_entry)) {
        // Uop is not sent to recycle port here.
        // There is a corresponding STA down the store pipe; recycling occurs through store recycle port.
    } else {
        // dl1 tlb hit: now we know whether the page is tagged or not
        sob_entry.page_type = tagged_pages ? PageType::TAGGED : PageType::UNTAGGED;

        bool speculative_atomic_load = speculative_atomic_load_allowed(sob_entry);
        if (!sob_entry.recycle && speculative_atomic_load) {
            if (auto matches = forwarding_matches(sob_entry); matches.any() || matches.wake) {
                bool forwarding_match =
                    (atomic_load_speculative_abort_unknown && matches.num_older_unknown)
                    || (atomic_load_speculative_abort_matches ? (matches.num_older_unknown || matches.num_older_partial)
                                                              : matches.wake == WakeCode::STLR);

                if (forwarding_match || (matches.wake == WakeCode::STLR && atomic_load_speculative_abort_matches)) {
                    sob_entry.go_to_sleep(matches.wake.value_or(WakeCode::STLR));
                    recycle_sta_load(sob_entry, forwarding_match ? RecycleReason::ATOMIC_FWD : RecycleReason::BARRIER);
                    uop_st_speculative_atomic_abort_event(sob_entry.uop);
                }
            }
        }

        // Don't send an L2 request for a faulting load, just fake an L1 hit so we can complete it
        if (!speculative_atomic_load || !sob_entry.recycle) {
            bool speculative_only = prefetch_sta == STAPrefetchPolicy::ATTEMPT && !enable_mte;
            CacheResult cache_result;
            if (sob_entry.uop->faulted || writeback_only) {
                cache_result.hit = true;
            } else {
                cache_result = l1d_cache_access(sob_entry, speculative_only);
                cache_result.sleep_if_needed(sob_entry);
                if (cache_result.needs_recycle())
                    recycle_sta_load(sob_entry, *cache_result.recycle_reason);
            }

            if (may_writeback) {
                auto conflicts = l1d_cache_conflicts.load_cache_conflicts(sob_entry.uop,
                                                                          *sob_entry.uop->virt_addr,
                                                                          *sob_entry.uop->phys_addr,
                                                                          sob_entry.mem_datasize,
                                                                          sob_entry.dl1_port);

                if (conflicts.read && (!read_conflict_can_miss || cache_result))
                    recycle_sta_load(sob_entry, RecycleReason::LD_CONFLICT, true);
                else if (conflicts.write)
                    recycle_sta_load(sob_entry, RecycleReason::ST_CONFLICT, true);
                else if (conflicts.fill)
                    recycle_sta_load(sob_entry, RecycleReason::FILL_CONFLICT, true);

                cache_result.hit &= !sob_entry.recycle;

                if (cache_result) {
                    l1d_cache_conflicts.load_cache_read(sob_entry.uop,
                                                        *sob_entry.uop->virt_addr,
                                                        sob_entry.mem_datasize,
                                                        sob_entry.dl1_port,
                                                        conflicts);
                }
            }

            if (cache_result || speculative_only
                || (enable_early_prfm && sob_entry.uop->is_sw_prefetch() && !sob_entry.recycle))
            {
                load_complete.put(sob_entry.uop);
                return;
            } else if (!sob_entry.recycle
                       && (speculative_atomic_load || sob_entry.tagged() || prefetch_sta == STAPrefetchPolicy::REQUIRE))
            {
                // l1d_cache_access might already have set the sta-load to recycle -- if not, recycle it here so that
                // it can't start executing again until it finishes the pipeline.
                recycle_sta_load(sob_entry, RecycleReason::L1_MISS);
            }
        }
    }

    if (enable_load_cancel && may_writeback)
        load_cancel_out->put(sob_entry.uop);
}

bool LSU::sob_has_older_barrier(const LOBEntry& lob_entry) const
{
    for (const auto& [_, sob_entry] : sob) {
        const auto& uop = sob_entry.uop;
        if (!uop)
            continue;
        if (!uop->is_older(lob_entry.uop->alloc_id))
            break;

        // Look for an older uop which is a barrier, or if there is a store marked 'ordered' by a previous DMB ST,
        // or if a DMB ST has committed but we have not yet marked the next store as ordered (next_store_is_ordered)
        if (sob_entry.barrier || sob_entry.ordered || next_store_is_ordered) {
            return true;
        }
    }

    return false;
}

bool LSU::can_load_combine(const LOBEntry& lob_entry)
{
    if (!load_combining)
        return false;

    // Can't do more than 2 loads per partition
    auto pipe = lob_entry.uop->sch.lsu_scheduler_pipe();
    if (loads_per_pipe[pipe] >= (load_combining.value_or(1) - 1))
        return false;

    if (combine_always
        || l1d_cache_conflicts.can_load_combine(lob_entry.dl1_port,
                                                {*lob_entry.uop->virt_addr, lob_entry.mem_datasize}))
    {
        ++loads_per_pipe[pipe];
        return true;
    }

    return false;
}

bool LSU::can_load_schedule(const LOBEntry& lob_entry)
{
    auto now = get_cycle();
    auto bypass_cycle = lob_entry.issue_cycle + lob_bypass_delay;
    auto first_exec_cycle_after_bypass = bypass_cycle + 1 + delay_after_lob_bypass;

    if (!lob_entry.is_ready()) {
        return false;
    } else if ((loads_sched_pipe & lob_entry.pipe_mask()) && !can_load_combine(lob_entry)) {
        return false; // LOB pipe already scheduled by another load
    } else if (load_fill_conflicts && l2c_refill_pipe_consumed(l2c_refill_load_arbitration, lob_entry.pipe_mask())) {
        return false; // LOB pipe already used by L2DC refill
    } else if (lob_entry.uop->is_exclusive() && !is_non_speculative(lob_entry)) {
        // Load exclusives can only execute non-speculatively
        // which means the uop needs to be at the top of the ROB.
        return false;
    } else if (store_prefetch_nonspec && lob_entry.uop->is_store_prefetch() && !is_non_speculative(lob_entry)) {
        return false;
    } else if (now < bypass_cycle) {
        // bypass_cycle is the earliest this load can execute
        return false;
    } else if (now != bypass_cycle && now < first_exec_cycle_after_bypass) {
        // If the load doesn't bypass, there may be a delay before it can schedule out of the LOB
        return false;
    } else if (fill_conflict_blocks_schedule && l1d_cache_conflicts.load_port1_refill_conflict(lob_entry.dl1_port)) {
        // If we can't schedule pipe1 due to a fill conflict, and there was a load which could have scheduled,
        // tell IDR to push more loads to the other pipe. Note that this check comes last so that we know that
        // this load could have scheduled if not for the conflict.
        if (lob_balancing && lob_balance_out->can_put())
            lob_balance_out->put(true);
        return false; // Avoid recycles due to port1 conflicts
    }
    return true;
}

void LSU::sleep_load_on_store(LOBEntry& lob_entry, uint64_t st_alloc_id, WakeCode code, RecycleReason reason)
{
    lob_entry.go_to_sleep(code, st_alloc_id);
    recycle(lob_entry, ld_recycle_complete.out);

    auto& wakeup_id = (code == WakeCode::ST_DATA) ? std_wakeup_ld_data : sta_wakeup_ld_data;
    uop_ld_recycle_event(lob_entry.uop, wakeup_id(st_alloc_id), ld_recycle_reason(reason));
}

void LSU::sleep_load_on_store(
    LOBEntry& lob_entry, SOBEntry& sob_entry, uint64_t st_alloc_id, WakeCode code, RecycleReason reason)
{
    sleep_load_on_store(lob_entry, st_alloc_id, code, reason);

    // Panthera perf: track which SOB entries might have a load sleeping on them so that we don't have to scan
    // the whole LOB every time a store gets its data or retires. Unfortunately this doesn't work for ST_ADDR
    // sleep because we might be sleeping on a store which has not even allocated into the SOB yet.
    if (code == WakeCode::ST_RETIRE) {
        // A load in ST_RETIRE sleep will cause a delayed-retirement store to go ahead and retire
        delayed_stores = 0;
    } else if (code == WakeCode::ST_DATA) {
        sob_entry.st_data_load = true;
    }
}

void LSU::SLOBEntry::recycle_into_scheduler()
{
    AMPERE_ASSERT(
        recycle, "{} {} completed recycle pipe but is not recycling", uop->is_load() ? "Load" : "STA", uop->alloc_id);
    recycle = false;
    executing = false;
    suppress_cancel = false;
}

void LSU::recycle(SLOBEntry& entry, Out<uint64_t>& recycle_pipe)
{
    AMPERE_ASSERT(!entry.recycle, "{} {} already recycled", entry.uop->is_load() ? "Load" : "STA", entry.uop->alloc_id);
    entry.recycle = true;
    recycle_pipe->put(entry.uop->alloc_id);
}

void LSU::recycle_load(LOBEntry& lob_entry, RecycleReason reason, bool fast)
{
    if (slow_write_conflict_port1 && reason == RecycleReason::ST_CONFLICT
        && lob_entry.uop->sch.lsu_scheduler_pipe() == 1)
        recycle(lob_entry, ld_slow_recycle_complete.out);
    else if (fast_fill_conflict_port1 && reason == RecycleReason::FILL_CONFLICT && lob_entry.dl1_port == 1) {
        recycle(lob_entry, ld_fill_recycle_complete.out);

        // This "recycle" is so fast that we don't even send a wake to IXU
        lob_entry.suppress_cancel = true;
    } else
        recycle(lob_entry, fast ? ld_fast_recycle_complete.out : ld_recycle_complete.out);
    uop_ld_recycle_event(lob_entry.uop, ld_recycle_reason(reason));
}

void LSU::recycle_sta_load(SOBEntry& sob_entry, RecycleReason reason, bool fast)
{
    recycle(sob_entry, fast ? ld_fast_recycle_complete.out : ld_recycle_complete.out);
    uop_st_recycle_event(sob_entry.uop, st_recycle_reason(reason));
}

void LSU::recycle_store(SOBEntry& sob_entry, RecycleReason reason, bool force)
{
    if (force)
        sob_entry.must_recycle = true;
    recycle(sob_entry, st_recycle_complete.out);
    uop_st_recycle_event(sob_entry.uop, st_recycle_reason(reason));
}

void LSU::do_stfwd_walk(LOBEntry& lob_entry, uint64_t st_alloc_id)
{
    uop_ld_st_fwd_walk_event(lob_entry.uop, ld_st_fwd_entry_data(st_alloc_id));
    lob_entry.oldest_skipped_store = st_alloc_id;
    recycle_load(lob_entry, RecycleReason::STFWD_WALK);
}

bool LSU::acquire_mlb_or_sleep(LOBEntry& lob_entry, Out<uint64_t>& load_recycle_pipe)
{
    if (!lob_entry.has_mlb) {
        // Only the oldest load in the LOB can take the last MLB entry
        unsigned remaining = mlb_entries - mlb_in_use;
        if (!remaining || (remaining <= 1 && lob_entry.uop->alloc_id != lob.begin()->first)) {
            lob_entry.go_to_sleep(WakeCode::BASES_LOADED);
            recycle(lob_entry, load_recycle_pipe);
            uop_ld_recycle_event(lob_entry.uop, ld_recycle_reason(RecycleReason::BASES_LOADED));
            return true;
        } else {
            lob_entry.has_mlb = true;
            mlb_in_use++;
            AMPERE_ASSERT(mlb_in_use <= mlb_entries, "Too many MLB entries in use");
        }
    }

    return false;
}

std::pair<bool, LSU::ForwardingMatches::Match*> LSU::ForwardingMatches::check(uint64_t alloc_id,
                                                                              const SOBEntry& sob_entry,
                                                                              const SLOBEntry& entry)
{
    if (!sob_entry.can_forward()) {
        if (entry.uop->is_atomic_load())
            num_older_unknown++;
        return {true, nullptr};
    }

    const auto& uop = entry.uop;
    const auto& st_uop = sob_entry.uop;

    // Get the right VA for the load and store in case of splits
    auto load_va = entry.virt_addr();
    auto store_va = sob_entry.virt_addr();
    Match match(alloc_id, st_uop->is_stack_source());

    // For the any-bytes check we are only looking at the untranslated bits of the VA
    // (this is the partial address match check)
    match.any_bytes =
        address_range_a_intersects_b(store_va & 0xfff, st_uop->mem_datasize(), load_va & 0xfff, uop->mem_datasize(), 1);

    // For 4k splits the above logic will be false in case of partial address matches
    // so we need to take care of 4k splits separately
    match.any_bytes |= (sob_entry.split_4k
                        && address_range_a_intersects_b(store_va.next_line_address() & 0xfff,
                                                        sob_entry.split_high_half_mem_datasize(),
                                                        load_va & 0xfff,
                                                        uop->mem_datasize(),
                                                        1))
                       || (entry.split_4k
                           && address_range_a_intersects_b(store_va & 0xfff,
                                                           st_uop->mem_datasize(),
                                                           load_va.next_line_address() & 0xfff,
                                                           entry.split_high_half_mem_datasize(),
                                                           1));

    if (match.any_bytes) {
        // Figure out which of the load's bytes are provided by this store
        auto first_covered_byte = std::max(load_va, store_va);
        auto last_load_byte = entry.virt_addr_last_byte();
        auto last_store_byte = sob_entry.virt_addr_last_byte();
        auto last_covered_byte = std::min(last_load_byte, last_store_byte);
        unsigned num_covered_bytes = last_covered_byte - first_covered_byte + 1;
        match.covered_bytes = ((1ULL << num_covered_bytes) - 1) << (first_covered_byte - load_va);
    }

    // Store provides at least some of the data for the load, based on the full address
    match.partial_data_match = match.true_match =
        address_range_a_intersects_b(store_va, st_uop->mem_datasize(), load_va, uop->mem_datasize(), 1);

    // Check whether the store provides all the bytes for the load. This should really be based
    // on PA instead of VA, but we will only use it below if the PA is valid and VA-aliasing
    // is unlikely to happen in traces.
    match.all_bytes = address_range_a_covers_b(store_va, st_uop->mem_datasize(), load_va, uop->mem_datasize());

    // A load which needs data from the high half of a store which splits a 4k boundary
    // has to wait for the store to retire
    if (sob_entry.split_4k || entry.split_4k) {
        auto last_load_byte = entry.virt_addr_last_byte();
        auto last_store_byte = sob_entry.virt_addr_last_byte();
        match.forward_from_4k_split = last_load_byte.same_page_4k(last_store_byte);
    }

    if (!match.any_potential_overlap())
        return {false, nullptr};

    return {false, &matches.emplace_back(match)};
}

LSU::ForwardingMatches LSU::forwarding_matches(SLOBEntry& entry, uint64_t oldest_skipped)
{
    auto& uop = entry.uop;
    auto alloc_id = uop->alloc_id;
    ForwardingMatches matches(entry.covered_bytes);

    // true = predict we will forward, so wait for older unknown stores
    // false = predict we will not forward, so skip older unknown stores
    matches.forwarding_predicted = sf_predictor.predict(uop);

    if (entry.uop->is_acquire() && !acquire_can_skip) {
        // On Furiosa and Ripley, load-acquire cannot skip unknown stores, so override the prediction to always wait
        matches.forwarding_predicted = true;
    }

    for (auto& it : boost::adaptors::reverse(preview_rob)) {
        auto& st_uop = it.second;

        // We make a first pass through the SOB to count the number of unknown addresses and stores
        // with a partial address match to this load. To avoid having to do all the address calculations
        // again on the second pass, we also record all address-related information required for forwarding
        // in the SOB entry. These flags are only temporary, valid just within this function.
        if (st_uop->is_store() && st_uop->is_older(alloc_id) && st_uop->is_older(oldest_skipped)) {
            auto sob_iter = sob.find(st_uop->alloc_id);

            if (sob_iter == sob.end() || !sob_iter->second.uop) {
                // Store has not yet been written into the SOB, so even its VA is totally unknown
                auto match = matches.check_unknown(st_uop->alloc_id, st_uop->is_stack_source());
                match->true_match = address_range_a_intersects_b(
                    *st_uop->virt_addr, st_uop->mem_datasize(), *entry.uop->virt_addr, entry.uop->mem_datasize(), 1);
                match->true_match |= (entry.uop->is_acquire() && st_uop->is_release());
            } else {
                auto& sob_entry = sob_iter->second;
                auto [skip, match] = matches.check(st_uop->alloc_id, sob_entry, entry);
                if (skip)
                    continue;

                // Load-acquire: an acquire cannot execute until older store-releases are done, meaning they are out of
                // the SOB and the L2. An acquire which needs to wait goes to STLR sleep until the falling edge of the
                // l2_pending_store_release signal, or until it is non-spec. Since the SOB entry is not deallocated for
                // 3 cycles after the store is sent to the L2, there should be no hole where a load-acquire could
                // execute while a store-release is in flight between LSU and L2.
                //
                // On Furiosa and Ripley, acquires sleep on all older barriers, not just releases.
                // Also, acquires will sleep when they see an older atomic/exclusive release which has completed in the
                // L2 but is still waiting for its data to come back before it can commit. In both these cases, either
                // l2_pending_store_release will not toggle or we go to sleep after it has already deasserted, so the
                // acquire has to wait for non-spec to wake up. Both these issues are fixed in Trinity
                // (acquire_sleep_on_done_exclusive=0 and acquire_sleep_on_all_barriers=0).
                if (entry.uop->is_acquire()) {
                    bool store_requires_sleep = acquire_sleep_on_all_barriers
                                                    ? (sob_entry.barrier || sob_entry.ordered || next_store_is_ordered)
                                                    : st_uop->is_release();
                    bool can_ignore_done_exclusive = (st_uop->is_exclusive() || st_uop->is_atomic_load())
                                                     && sob_entry.exclusive_is_ready_to_complete()
                                                     && !acquire_sleep_on_done_exclusive;
                    bool stlr_sleep = store_requires_sleep && !can_ignore_done_exclusive
                                      && (!(entry.uop->is_atomic_load() ? atomic_load_speculative_abort_matches
                                                                        : acquire_sleep_on_matching_release)
                                          || (match && match->partial_data_match));

                    if (stlr_sleep || l2_pending_store_release) {
                        matches.wake = WakeCode::STLR;
                        break;
                    }
                }

                if (match && match->any_bytes) {
                    bool new_bytes_covered = match->covered_bytes & (~matches.accum_covered_bytes);
                    bool must_walk = !only_walk_stores_providing_new_bytes;
                    if (no_walk_for_false_positive_lower_va_bit_match)
                        must_walk |= (match->partial_data_match && new_bytes_covered);
                    else
                        must_walk |= new_bytes_covered;

                    if (must_walk) {
                        if (sob_entry.pa_valid()) {
                            matches.potentially_matching(st_uop->alloc_id, match->covered_bytes);
                        } else {
                            matches.potentially_matching(st_uop->alloc_id);
                        }
                    }
                }
            }
        }
    }

    return matches;
}

LSU::ForwardingResult LSU::load_store_forward_checks(LOBEntry& lob_entry)
{
    if (!lob_entry.can_forward())
        return {};

    auto matches = forwarding_matches(lob_entry, lob_entry.oldest_skipped_store);
    lob_entry.ld_st_fwd_prediction = matches.forwarding_predicted;

    if (matches.wake) {
        if (matches.wake == WakeCode::STLR)
            return {*matches.wake, RecycleReason::BARRIER};
        return {*matches.wake};
    }

    // Now that we know how many older unknown and partial-match addresses there are, make a second pass
    // through the SOB to actually do forwarding or sleep.
    for (const auto& match : matches.matches) {
        auto sob_iter = sob.find(match.alloc_id);
        bool unknown = sob_iter == sob.end() || !sob_iter->second.uop;
        bool partial_match = !unknown && !sob_iter->second.pa_valid() && match.any_bytes;
        if (unknown || partial_match) {
            // Address is totally unknown, or PA is not ready and 11:0 of the load and store overlap

            if (enable_oracle_store_forwarding) {
                if (match.true_match)
                    return {WakeCode::ST_ADDR, RecycleReason::UNKNOWN_STA, match.alloc_id};
                continue;
            }

            // Cases where we go to ST_ADDR sleep:
            // - predictor says wait
            // - this is the low half of a split and there are older partial matches
            //     (even if splits are allowed to skip, they can't if there are older partials)
            //   and there are such stores
            if (always_wait_on_partial_match && partial_match) {
                // In this case we always wait if the low bits of the address match, regardless of prediction.
                // Because we don't set lob_entry.slept_on_store, we will not train the predictor if this
                // load ends up not forwarding.
                ev_load_predict_wait(lob_entry.uop, evd_unknown(unknown), evd_correct(match.true_match));
                return {WakeCode::ST_ADDR, RecycleReason::UNKNOWN_STA, match.alloc_id};
            } else if (lob_entry.ld_st_fwd_prediction
                       || (lob_entry.split && !lob_entry.split_lo_complete && matches.num_older_partial > 0))
            {
                if (lob_entry.ld_st_fwd_prediction) {
                    lob_entry.slept_on_store = true;
                    ev_load_predict_wait(lob_entry.uop, evd_unknown(unknown), evd_correct(match.true_match));
                }

                // If load-acquire cannot skip unknown stores, it also goes to ST_RETIRE sleep instead of ST_ADDR.
                auto wake =
                    (!acquire_can_skip && lob_entry.uop->is_acquire()) ? WakeCode::ST_RETIRE : WakeCode::ST_ADDR;
                return {wake, RecycleReason::UNKNOWN_STA, match.alloc_id};
            } else {
                if (stack_load_wait && lob_entry.uop->is_stack_source() && match.stack_source) {
                    ev_load_src_match_wait(lob_entry.uop, evd_unknown(unknown), evd_correct(match.true_match));
                    return {WakeCode::ST_ADDR, RecycleReason::UNKNOWN_STA, match.alloc_id};
                }

                lob_entry.skipped_unknown_store = true;
                lob_entry.skipped_unknown_store_this_pass = true;
                lob_entry.skipped_store_id = match.alloc_id;
                lob_entry.oldest_skipped_store = match.alloc_id;

                // On newer cores we only walk partial matches, ignoring any intervening unknowns.
                // By doing 'continue' here we will ignore this unknown and keep looking backward
                // for an older partial match.
                if (matches.num_older_partial == 0 || walk_partials_only)
                    continue;

                // On older cores, if there are older partial matches then we have to walk to skip an unknown
                return {ForwardingResult::WALK, match.alloc_id};
            }
        } else {
            // PA is ready and 11:0 of the load and store overlap (partial match)
            auto& sob_entry = sob_iter->second;

            // If we get here then the store ought to have completed its address translation and be a partial match
            AMPERE_ASSERT_ERROR(sob_entry.pa_valid(), "Forwarding before PA is ready");
            AMPERE_ASSERT(match.any_bytes, "Checking PA of a store which doesn't contribute any bytes");

            // For Connor, we do not allow a split load to forward from multiple older stores -- a split can
            // get data from one store, or from one store and the cache, but it can't walk backward over
            // multiple stores. (That's complicated because the MLB is being used both for the split and to
            // accumulate forwarding data at the same time.)
            bool can_partial_fwd =
                enable_partial_data_fwd
                && (split_fwd_from_multiple || !lob_entry.split || !matches.multiple(walk_partials_only));
            if (match.all_bytes || (match.partial_data_match && can_partial_fwd)) {
                // This store provides all the bytes for this load, or we allow forwarding of partial data

                // A load which needs data from the high half of a store which splits a 4k boundary
                // has to wait for the store to retire
                if (match.forward_from_4k_split)
                    return {WakeCode::ST_RETIRE, RecycleReason::FOUR_K_SPLIT, match.alloc_id};

                // A load which wants to forward from an atomic has to wait for it to retire since the
                // data returned by the atomic is the old, stale data from before memory was updated
                if (sob_entry.uop->is_atomic())
                    return {WakeCode::ST_RETIRE, RecycleReason::ATOMIC_FWD, match.alloc_id};

                // We can't forward from DC ZVA or exclusives
                if (sob_entry.uop->is_dc_zva() || sob_entry.uop->is_dc_va())
                    return {WakeCode::ST_RETIRE, RecycleReason::DCVA, match.alloc_id};
                if (lob_entry.uop->is_exclusive() || sob_entry.uop->is_exclusive())
                    return {WakeCode::ST_RETIRE, RecycleReason::EXCL_FWD, match.alloc_id};

                // Store forward candidate found but data not yet ready
                // Special case for a single-uop store which will complete in the next couple cycles --
                // in RTL the pipeline lines up so that the load is woken by the STA in XU03 and by the
                // time it gets to X05 the data is ready, so it won't go back to ST_DATA sleep.
                if ((!sob_entry.data_low_complete
                     || ((lob_entry.uop->is_pair() && sob_entry.pair) && !sob_entry.data_high_complete))
                    && !(sob_entry.fat_sus && sob_entry.addr_will_complete))
                {
                    return {WakeCode::ST_DATA, RecycleReason::STD_SLEEP, match.alloc_id};
                } else {
                    // make sure this store covers some bytes in this load
                    AMPERE_ASSERT(match.covered_bytes != 0, "Zero covered bytes");
                    AMPERE_ASSERT(match.covered_bytes <= 0xffff, "Too many covered bytes: 0x{:x}", match.covered_bytes);

                    // If this store provided new bytes to the load (not already provided by some younger store),
                    // then we will forward from it.
                    if (match.covered_bytes & (~lob_entry.covered_bytes)) {
                        uop_ld_st_fwd_event(lob_entry.uop, ld_st_fwd_entry_data(match.alloc_id));
                        pmu_event(lob_entry.uop, pmu_num_data(archx::PMU::LD_FROM_ST_FWD));
                        AMPERE_ASSERT(!lob_entry.uop->is_exclusive(), "Exclusives should never forward");

                        lob_entry.forwarded_from_store = true;
                        if (!match.all_bytes)
                            ev_partial_data_forward(lob_entry.uop);
                    }

                    lob_entry.covered_bytes |= match.covered_bytes;

                    if (lob_entry.youngest_forwarding_store == 0)
                        lob_entry.youngest_forwarding_store = match.alloc_id;

                    bool all_bytes_covered = lob_entry.all_bytes_covered();
                    AMPERE_ASSERT_ERROR(!match.all_bytes || all_bytes_covered,
                                        "SOB entry provided all bytes, but not all bytes covered");

                    if (all_bytes_covered && (match.all_bytes || partial_fwd_early_exit)) {
                        // There are more older partial address matches, but this partial-data-match store provided the
                        // final bytes needed by this load, so it can stop walking.
                        if (!match.all_bytes && matches.multiple(walk_partials_only))
                            ev_partial_data_early_exit(lob_entry.uop);

                        // Normal forwarding -- load got all its data and is done
                        return {ForwardingResult::FULL, match.alloc_id};
                    } else if (matches.multiple(walk_partials_only)) {
                        return {ForwardingResult::MULTIPLE_PARTIAL, match.alloc_id};
                    }

                    return {ForwardingResult::PARTIAL, match.alloc_id};
                }
            } else if (match.partial_data_match && !can_partial_fwd) {
                // This store provides some bytes to the load but did not provide all bytes,
                // so the load has to wait for the store to retire and then get the data from the cache
                return {WakeCode::ST_RETIRE, RecycleReason::PARTIAL_DATA, match.alloc_id};
            } else if (matches.multiple(walk_partials_only)) {
                // This store was a partial address match -- the low address bits said that the
                // load and store overlap, but the full address check found that they do not.
                // Also, there are more older partial matches or unknown addresses which we
                // haven't checked yet. Recycle and walk backward to the next oldest store.
                return {ForwardingResult::WALK, match.alloc_id};
            }
        }
    }

    return {};
}

LSU::ForwardingResult LSU::load_store_forward(LOBEntry& lob_entry)
{
    auto forwarding = load_store_forward_checks(lob_entry);

    // Get a split buffer to hold on to the partial data
    if (forwarding.result == ForwardingResult::MULTIPLE_PARTIAL) {
        auto slept = acquire_mlb_or_sleep(lob_entry, ld_recycle_complete.out);
        // This load doesn't have all its data yet, and there are more older address matches,
        // so we have to walk them.
        if (!slept)
            forwarding.result = ForwardingResult::WALK;
    }

    // PWC forwarding check
    std::optional<uint64_t> matching_pwc_id;
    if (pwc && lob_entry.uop->phys_addr) {
        if (pwc->can_forward()
            && (forwarding.result == ForwardingResult::NONE || forwarding.result == ForwardingResult::PARTIAL))
        {
            auto [blocked, pwc_id, mask] =
                pwc->check_matches(lob_entry.uop, *lob_entry.uop->phys_addr, lob_entry.mem_datasize);

            if (blocked != LSUPWC::BlockReason::NONE) {
                forwarding.result = ForwardingResult::SLEPT;
                forwarding.recycle_reason =
                    blocked == LSUPWC::BlockReason::ORDERED ? RecycleReason::PWC_ORDERED : RecycleReason::PWC_MULTIPLE;

                if (pwc->can_wakeup()) {
                    if (pwc->can_wakeup_precise()) {
                        if (!pwc_response->find_if([&](const auto& response) {
                                return response.id == *matching_pwc_id && response.done();
                            }))
                        {
                            forwarding.wake = WakeCode::PWC;
                            matching_pwc_id = pwc_id;
                        }
                    } else {
                        forwarding.wake = WakeCode::PWC;
                    }
                } else if (forwarding.matched_store()) {
                    forwarding.wake = WakeCode::ST_RETIRE;
                }
            } else if (mask) {
                lob_entry.covered_bytes |= mask;
                forwarding.result = lob_entry.all_bytes_covered() ? ForwardingResult::FULL : ForwardingResult::PARTIAL;
                uop_ld_pwc_fwd_event(lob_entry.uop);
            }
        }
    }

    // Forwarding responses
    if (forwarding.result == ForwardingResult::WALK) {
        do_stfwd_walk(lob_entry, forwarding.store_alloc_id);
    } else if (forwarding.result == ForwardingResult::SLEPT) {
        if (matching_pwc_id) {
            sleep_load_on_store(lob_entry, *matching_pwc_id, *forwarding.wake, *forwarding.recycle_reason);
        } else if (forwarding.matched_store()) {
            if (auto sob_iter = sob.find(forwarding.store_alloc_id); sob_iter != sob.end()) {
                auto& sob_entry = sob_iter->second;
                sleep_load_on_store(
                    lob_entry, sob_entry, forwarding.store_alloc_id, *forwarding.wake, *forwarding.recycle_reason);
            } else {
                sleep_load_on_store(lob_entry, forwarding.store_alloc_id, *forwarding.wake, *forwarding.recycle_reason);
            }

            if (lob_entry.uop->is_acquire() && *forwarding.wake == WakeCode::ST_RETIRE)
                uop_ldar_stret_sleep(lob_entry.uop);
        } else {
            if (forwarding.wake)
                lob_entry.go_to_sleep(*forwarding.wake);
            if (forwarding.recycle_reason) {
                recycle_load(lob_entry, *forwarding.recycle_reason, !forwarding.wake);
                if (forwarding.recycle_reason == RecycleReason::BARRIER)
                    uop_ld_stlr_sleep_event(lob_entry.uop);
            }
        }
    }

    return forwarding;
}

bool LSU::can_send_dmmu_request(uint8_t channel_mask) const
{
    return mmu_miss_request->can_put() && (mmu_miss_request_channels & channel_mask) == 0;
}

void LSU::send_dmmu_request(MMUReq req, unsigned channel)
{
    req->channel = channel;
    mmu_miss_request->put(std::move(req));
    mmu_miss_request_channels |= 1ULL << channel;
}

template<class Entry>
bool LSU::dl1_tlb_access(Entry& e)
{
    // Translate UOP virtual address to physical address
    //  - If miss, sleep load until response from dl1tlb
    auto tag = L1TLBTag<VirtAddr>(*e.uop->virt_addr, driver_port->asid());
    auto dl1_tlb_result = dl1tlb.lookup(e.uop, tag);

    pmu_event(e.uop, pmu_num_data(archx::PMU::L1D_TLB));

    if (e.uop->is_sw_prefetch())
        pmu_event(e.uop, pmu_num_data(archx::PMU::L1D_TLB_PRFM));
    else if constexpr (std::is_same_v<Entry, LOBEntry>)
        pmu_event(e.uop, pmu_num_data(archx::PMU::L1D_TLB_RD));
    else
        pmu_event(e.uop, pmu_num_data(archx::PMU::L1D_TLB_WR));

    bool lookup_required = true;
    if constexpr (std::is_same_v<Entry, SOBEntry>) {
        lookup_required = e.needs_sta_execute();
    }

    if (TranslationContext tr = {*e.uop->virt_addr, Bitfield(e.uop->ainst()->state().read(TCR_EL1))};
        (e.needs_canonical_check() && !tr.canonical()) || e.uop->faulted)
    {
        if (!e.uop->faulted) {
            pmu_event(e.uop, pmu_num_data(archx::PMU::L1D_TLB_MISS));
            canonical_fault(e.uop);
            e.uop->faulted = true;
        }
        // If this uop already faulted and is coming back around, just let it complete
        e.uop->phys_addr = 0x0_pa;
        e.page_size = PageSize::Size::PS_4KB;
    } else if (e.uop->is_store_barrier() || !lookup_required) {
        // don't do anything for a barrier -- these don't look up the TLB
        e.uop->phys_addr = 0x0_pa;
    } else if (!dl1_tlb_result) {
        // DL1_TLB miss
        pmu_event(e.uop, pmu_num_data(archx::PMU::L1D_TLB_MISS));
        dl1_tlb_miss(e);
        return false;
    } else { // DL1_TLB hit
        auto page_base = dl1_tlb_result->phys_addr.page_address(dl1_tlb_result->page_size).value();
        auto untranslated_bits = e.uop->virt_addr->extract_range(dl1_tlb_result->page_size.page_bits() - 1, 0);
        e.uop->phys_addr = PhysAddr(page_base | untranslated_bits);
        e.page_size = dl1_tlb_result->page_size.value();
        if (dl1_tlb_result->phys_addr == 0x0_pa) {
            e.uop->phys_addr = e.uop->instructions.front()->archx_instruction->physical_address();
        }
    }
    return true;
}

void LSU::l1d_cache_refill(MemResp mem_resp,
                           VirtAddr virt_addr,
                           bool modified,
                           bool exclusive,
                           bool hw_prefetch,
                           bool sw_prefetch,
                           PrefetchType prefetch_type)
{
    auto phys_addr = mem_resp->phys_addr;

    // The L2 might have sent us a line and then immediately snooped it out. To avoid breaking inclusion, don't allow
    // recently snooped lines to fill the cache.
    auto s = snooped_lines.begin();
    while (s != snooped_lines.end()) {
        if (get_cycle() - s->second > snoop_exclusion_window) {
            s = snooped_lines.erase(s);
            continue;
        }
        if (s->first.same_cache_line(phys_addr))
            return;
        s++;
    }

    L1DCacheline line(phys_addr.line_address(), prefetch_type, mem_resp->unique, modified);
    PhysAddr new_phys_line{phys_addr.line_address()};

    // Count REFILL PMU stats if demand request
    // L1D_CACHE_REFILL is computed in rollup_formula.py as L1D_CACHE_REFILL_RW + L1D_CACHE_REFILL_PRFM
    if ((hw_prefetch || sw_prefetch) && separate_l1d_prfm_pmus) {
        pmu_event(mem_resp, pmu_num_data(archx::PMU::L1D_CACHE_REFILL_PRFM));
    } else {
        // Since our L1 is write-through, we never have fills due to a write
        // L1D_CACHE_REFILL_RW is computed in rollup_formula.py as L1D_CACHE_REFILL_RD
        pmu_event(mem_resp, pmu_num_data(archx::PMU::L1D_CACHE_REFILL_RD));
    }

    if (exclusive) {
        // Since exclusives skip lookup, there may be a way that should be overwritten instead of filled
        if (auto result = l1d_cache.lookup(mem_resp, {virt_addr, new_phys_line}, false, true); result) {
            if (result->modified) {
                send_l2_evict_request(mem_resp, *result);
                ev_inval_modified_refill(mem_resp);
            }

            l1d_cache.update_value(mem_resp, {virt_addr, new_phys_line}, line);
            return;
        }
    }

    auto victim_value = l1d_cache.refill(mem_resp, {virt_addr, new_phys_line}, line);
    if (victim_value && victim_value->modified) {
        send_l2_evict_request(mem_resp, *victim_value);
        ev_inval_modified_refill(mem_resp);
    }
}

void LSU::l1d_cache_invalidate_aliases(Txn tx, VirtAddr virt_addr, PhysAddr phys_addr, bool exclude_self)
{
    auto aliases = l1d_cache.aliases(virt_addr, phys_addr, exclude_self);
    for (const auto& set_addr : aliases) {
        auto line = l1d_cache.invalidate(tx, {set_addr, phys_addr});
        if (line && line->modified) {
            send_l2_evict_request(tx, *line);
            l1d_cache_conflicts.try_eviction(set_addr);
            ev_inval_modified_alias(tx);
        }
    }
}

// If spec_lookup is true then this is a parallel L1 lookup for a load which was handled by the
// store forwarding logic, so its cache access turns into a prefetch
// return value 1: whether hit or miss in L1D
// return value 2: whether hits a prefetched cacheline in L1D, and, if yes, which prefetcher brought in the cacheline
template<class Entry>
LSU::CacheResult LSU::l1d_cache_access(Entry& e, bool spec_lookup)
{
    // Exclusives and Atomics always miss the L1 cache.
    bool tag_check_only = false;
    bool speculative_atomic = false;
    if constexpr (std::is_same_v<Entry, SOBEntry>) {
        speculative_atomic = speculative_atomic_load_allowed(e);
        tag_check_only = enable_mte && !e.exclusive_complete && !speculative_atomic;
    }
    if ((e.uop->is_exclusive() || (write_through_atomic(e.uop) && !speculative_atomic)) && !tag_check_only) {
        return l1d_cache_miss(e, spec_lookup);
    }

    // L1D cache lookup
    //  - If miss, sleep load until response from l2c
    auto l1d_cache_result = l1d_cache.lookup(
        e.uop, {*e.uop->virt_addr, *e.uop->phys_addr}, true, false, false, e.uop->phys_addr->line_address().value());

    // L1D VA tag miss
    if (!l1d_cache_result) {
        return l1d_cache_miss(e, spec_lookup);
    } else if (!l1d_cache.enable_perfect_mode
               && e.uop->phys_addr->line_address()
                      != (*l1d_cache_result).addr) { // L1D PA tag miss (not checked for perfect L1D)
        uop_ld_pa_tag_miss_event(e.uop);
        return l1d_cache_miss(e, spec_lookup);
    } else { // L1D hit

        // L1D_CACHE is counted only once per load (misses/retries are not counted)
        // L1D_CACHE is computed in rollup_formula.py as L1D_CACHE_RW + L1D_CACHE_PRFM
        if (e.uop->is_sw_prefetch() && separate_l1d_prfm_pmus) {
            pmu_event(e.uop, pmu_num_data(archx::PMU::L1D_CACHE_PRFM));
        } else {
            // L1D_CACHE_RW is computed in rollup_formula.py as L1D_CACHE_RD + L1D_CACHE_WR
            if (e.uop->is_load())
                pmu_event(e.uop, pmu_num_data(archx::PMU::L1D_CACHE_RD));
            else if (e.uop->is_store())
                pmu_event(e.uop, pmu_num_data(archx::PMU::L1D_CACHE_WR));
        }

        if (enable_coherency_validation)
            coherency_validator->line_present(e.uop->phys_addr->line_address(), false /*is instruction*/);

        // identify prefetch_hit
        auto& type = (*l1d_cache_result).prefetch_type;
        if (type != PrefetchType::NONE) {
            if constexpr (std::is_same_v<Entry, LOBEntry>) {
                BackendMemReq req = {e.uop};
                prefetcher->execute(e.uop->alloc_id, req, true, true, true, type);
            }

            type = PrefetchType::NONE; // clear prefetch bit
            return CacheResult(type);  // L1D hit and prefetch hit-type (or not)
        }

        return CacheResult(PrefetchType::NONE); // L1D hit but not prefetch hit
    }
}

void LSU::prefetch_issue(L1PrefetchEntry next_pf, PrefetchType type)
{
    // Figure out what pipe this prefetch will execute on, for correct fill tracking
    unsigned pipe = 0;
    unsigned ports = prefetch_uses_store_pipes ? sta_pipes() : load_pipes;
    uint8_t taken_pipes = prefetch_uses_store_pipes ? stores_sched_pipe : loads_sched_pipe;
    if (prefetch_uses_store_pipes ? sta_fill_conflicts : load_fill_conflicts)
        taken_pipes |=
            l2c_refill_pipes(prefetch_uses_store_pipes ? l2c_refill_store_arbitration : l2c_refill_load_arbitration);

    for (; pipe < ports; ++pipe) {
        if ((taken_pipes & (1U << pipe)) == 0) {
            next_pf.pipe = pipe % ports;
            break;
        }
    }

    AMPERE_ASSERT(pipe < ports, "No {} pipe to schedule prefetch!", prefetch_uses_store_pipes ? "store" : "load");

    if (prefetch_uses_store_pipes) {
        stores_executed++;
        stores_sched_pipe |= next_pf.pipe_mask();
        store_pipe_occupancy_event(
            next_pf.parent_txn, pipe_partition(next_pf.pipe_mask()), pipe_occupant(PipeOccupant::PF));
    } else {
        load_pipes_used++;
        loads_sched_pipe |= next_pf.pipe_mask();
        load_pipe_occupancy_event(
            next_pf.parent_txn, pipe_partition(next_pf.pipe_mask()), pipe_occupant(PipeOccupant::PF));
    }

    prefetch_execute(next_pf, type);
}

void LSU::prefetch_schedule()
{
    // PCP and Gretch have trained and triggered, and prefetches may be sitting in the
    // on-deck queue. To model the side-door load pipeline, we add latency
    // here and do all the tlb/cache lookups after the trampoline.

    // Tag conflict with incoming fill just prevents any pf from scheduling
    if (fill_conflict_blocks_schedule && l1d_cache_conflicts.load_refill_conflict_check()) {
        return;
    }

    // only take as many as there are load slots this cycle.
    while (can_prefetch_schedule()) {
        if (auto pob_entry = pob.replay(curr_el); pob_entry) {
            // Priority 1: replay
            L1PrefetchEntry next_pf(pob_entry->virt_addr,
                                    pob_entry->trigger_uop,
                                    pob_entry->parent_txn,
                                    pob_entry->consumer_pc,
                                    pob_entry->l2_prefetch_type);
            prefetch_issue(next_pf, pob_entry->type);
        } else if (prefetcher->can_issue_prefetch() && prefetch_request->can_take()) {
            // Priority 2+: prefetch queue
            const auto& next_req = prefetch_request->peek();
            if (!prefetcher->can_issue_prefetch(next_req))
                break;

            auto req = prefetch_request->take();
            if (!req.exception_level || req.exception_level == curr_el) {
                auto next_pf = req.as_entry();
                prefetch_issue(next_pf, req.prefetch_type);
            } else {
                prefetcher->drop(req, PrefetchDropReason::EXCEPTION_LEVEL);
            }
        } else {
            break;
        }
    }
}

void LSU::complete_load_recycle(In<uint64_t>& load_recycle_in)
{
    while (load_recycle_in->can_take()) {
        // Completing LOB entry may have been erased due to flush

        // first search in LOB
        uint64_t alloc_id = load_recycle_in->take();
        if (auto lob_iter = lob.find(alloc_id); lob_iter != lob.end()) {
            auto& lob_entry = lob_iter->second;
            lob_entry.recycle_into_scheduler();

            if (lob_entry.wake_code == WakeCode::ST_DATA) {
                for (auto store_alloc_id : inflight_store_data_complete)
                    store_wakeup_dependent_load(lob_entry, store_alloc_id, WakeCode::ST_DATA);
            }
        }
        // search in SOB. STA-loads may have been selected for recycling.
        else if (auto sob_iter = sob.find(alloc_id); sob_iter != sob.end())
        {
            auto& sob_entry = sob_iter->second;
            AMPERE_ASSERT(
                sob_entry.recycle, "Sta-Load {} completed recycle pipe but is not recycling", sob_entry.uop->alloc_id);
            sob_entry.recycle = false;
            sob_entry.suppress_cancel = false;
            // Does not reset 'executing' field because STA-load is not independently schedulable, and STA may be in the
            // store pipeline. Example: STA-load recycling due to fill conflict will reach here sooner than store
            // pipeline is traversed.
        }
    }
}

// Final pipestage of load pipeline
void LSU::load_complete_stage()
{
    while (load_complete.can_take()) {
        // Determine if completion is handling normal load or L2 refill
        if (auto uop = load_complete.take(); uop)
            load_complete_uop(uop, false /* not a L2C refill */);
    }

    // Set loads marked to recycle as now eligible to reschedule (LD04)
    complete_load_recycle(ld_recycle_complete.in);
    complete_load_recycle(ld_fast_recycle_complete.in);
    complete_load_recycle(ld_slow_recycle_complete.in);
    complete_load_recycle(ld_fill_recycle_complete.in);
    complete_load_recycle(ld_split_recycle_complete.in);
}

void LSU::load_complete_uop(Uop uop, bool refill)
{
    // Completing LOB entry may have been erased due to flush
    if (auto lob_iter = lob.find(uop->alloc_id); lob_iter != lob.end()) {
        auto& lob_entry = lob_iter->second;

        ofb.reset_reservation(uop->alloc_id);

        if (lob_entry.split) {
            if (!lob_entry.split_lo_complete) {
                // A split load needs to allocate an MLB entry to hold the low half of the data while the high
                // half goes through the pipe. The last entry can only be consumed by the oldest load.
                auto slept = acquire_mlb_or_sleep(lob_entry, ld_split_recycle_complete.out);
                if (slept)
                    return;

                lob_entry.adjust_to_hi_half_addr();

                // Mark the low half complete and send the uop back through the pipeline
                recycle(lob_entry, ld_split_recycle_complete.out);
                uop_ld_recycle_event(lob_entry.uop, ld_recycle_reason(RecycleReason::SPLIT));

                uop_ld_cacheline_split_event(lob_entry.uop);
                // LDST_ALIGN_LAT is computed in rollup_formula.py as LD_ALIGN_LAT + ST_ALIGN_LAT
                pmu_event(lob_entry.uop, pmu_num_data(archx::PMU::LD_ALIGN_LAT));
                return;
            }
            /* Refill is priority over DL1 hit. Therefore, it does not check for availability of writeback port.
             * In fact, performing the check for a refill causes some LSU tests to fail. */
            else if (refill || can_writeback_uop(lob_entry.uop))
            {
                lob_entry.restore_base_addr();
            }
        }

        // normal path for loads
        /* Refill is priority over DL1 hit. Therefore, it does not check for availability of writeback port.
         * In fact, performing the check for a refill causes some LSU tests to fail. */
        // limit gather data returns to 1 per cycle
        if ((refill || can_writeback_uop(lob_entry.uop)) && (!lob_entry.uop->is_gather() || gather_wake_sent == 0)) {
            uop_ld_complete_event(lob_entry.uop);
            AMPERE_ASSERT(
                refill || lob_entry.executing, "Load {} completed but is not executing", lob_entry.uop->alloc_id);
            lob_entry.executing = false;

            if (lob_entry.has_mlb) {
                lob_entry.has_mlb = false;
                mlb_entry_release.put(lob_entry.uop->alloc_id);
            }

            writeback_load(lob_entry);

            if (gather_wake_limit && lob_entry.uop->is_gather()) {
                lob_entry.uop->get_gather_proxy()->lsu_sent();
                if (lob_entry.uop->get_gather_proxy()->lsu_done()) {
                    gather_wake_sent++;
                }
            }

            prefetcher->writeback(uop->alloc_id, {lob_entry.uop});

            if (lob_entry.slept_on_store || lob_entry.skipped_unknown_store) {
                auto prediction = lob_entry.ld_st_fwd_prediction;
                uop_ld_st_prediction_event(lob_entry.uop,
                                           sfp_index_data(sf_predictor.compute_set(lob_entry.uop->pc())),
                                           ld_st_prediction_data(prediction));
            }
            if (lob_entry.slept_on_store && !lob_entry.forwarded_from_store) {
                sf_predictor.train(lob_entry.uop, false);
                pmu_event(lob_entry.uop, pmu_num_data(archx::PMU::LSU_SFP_MISPRED_FWD));
                uop_ld_wait_error_event(lob_entry.uop,
                                        lob_entry.uop->pc(),
                                        sfp_index_data(sf_predictor.compute_set(lob_entry.uop->pc())),
                                        ld_st_prediction_data(lob_entry.ld_st_fwd_prediction));
            }

            if (lob_entry.uop->is_load() && lob_entry.uop->is_exclusive())
                pmu_event(lob_entry.uop, pmu_num_data(archx::PMU::LDREX_SPEC));
        } else {
            /* Load uop is not a L2C refill (likely a DL1 hit) and writeback port to IXU is not available.
             * This is an artificial scenario arising from Panthera's fake LSU pipeline; it does not happen in RTL.
             * Example: In Cycle X, Panthera performs a refill first and then completes a DL1 hit. It may happen
             * that both end up occupying the same LSU pipe and qualifying for the same issue pipe for
             * writeback to IXU. Cycle X's second uop is squashed and bogusly recycled in Panthera. */
            recycle(lob_entry, ld_split_recycle_complete.out);
            uop_ld_recycle_event(lob_entry.uop, ld_recycle_reason(RecycleReason::LD_CANCEL));
        }

        // Done with complete, now OK to release the LUB entry
        if (uop->lubid) {
            if (auto lub_iter = lub.find(uop->alloc_id); lub_iter != lub.end()) {
                auto& lub_entry = lub_iter->second;
                credit_return->put(std::move(lub_entry->lubid));
                lub.erase(lub_iter);
            } else {
                AMPERE_ERROR("Expected to still have LUB entry to deallocate when dealing with load completion");
            }
        }
    }
}

void LSU::sta_load_complete_stage()
{
    // Must be done before store/load_complete_stage, so we only peek
    for (unsigned i = 0, l = load_complete.num_ready(); i < l; ++i) {
        const auto& uop = load_complete.peek(i);
        if (!uop)
            continue;

        if (auto sob_iter = sob.find(uop->alloc_id); sob_iter != sob.end()) {
            // We arrive here upon DL1 hit of STA-load. Here, we just mark the fields of SOB entry.
            // 1) Moving to high half; 2) restoring addresses upon completing high half;
            // 3) writing back to inform gpc; 4) RAW scrub; 5) load wakeup are done in sta-execute completion stage.
            auto& sob_entry = sob_iter->second;
            AMPERE_ASSERT(sob_entry.page_type != PageType::UNKNOWN, "Page type is unknown after address translation.");

            if (speculative_atomic_load_allowed(sob_entry)) {
                AMPERE_ASSERT(can_writeback_uop(sob_entry.uop, true), "STA load should be able to writeback");
                writeback_sta_load(sob_entry, sob_entry.uop);
                uop_st_speculative_atomic_event(sob_entry.uop);
            } else if (!sob_entry.tagged()) {
                continue; // for an untagged page, we do not need to register staLd complete events
            }

            sta_load_complete_uop(sob_entry);
        }
    }
}

bool LSU::sta_load_complete_uop(SOBEntry& sob_entry, bool check_split_complete)
{
    bool low_half_of_split = sob_entry.split && !sob_entry.split_lo_sta_load_complete;
    if (low_half_of_split) {
        AMPERE_ASSERT_ERROR(!check_split_complete || !sob_entry.split_lo_complete,
                            "SOB entry moved to higher half without the low half's tag check being done\n");

        // Do not move to high half here. Store-complete-stage has already done it.
        sob_entry.split_lo_sta_load_complete = true;
        uop_sta_load_split_lo_complete_event(sob_entry.uop);
    } else if (!sob_entry.sta_load_complete) {
        // Not split or this is the high half of split.
        // In case of high half, do not restore addresses here.
        // Store-complete-stage has already done it.
        sob_entry.sta_load_complete = true;
        uop_sta_load_complete_event(sob_entry.uop);
        if (sob_entry.uop->is_atomic_load())
            uop_st_atomic_load_complete_event(sob_entry.uop);
    }

    return !low_half_of_split;
}

void LSU::sta_wakeup_stage()
{
    while (sta_wakeup.can_take()) {
        auto [st_alloc_id, scheduled] = sta_wakeup.take();
        auto sob_iter = sob.find(st_alloc_id);

        if (sob_iter != sob.end()) {
            auto& sob_entry = sob_iter->second;

            // Unless this store is the low half of a split, it is guaranteed to complete in 2 cycles
            // and any loads which it wakes up should be allowed to forward from it. Setting this flag
            // tells a load that it can forward from the store even though addr_complete is not set yet.
            if (scheduled && (!sob_entry.split || sob_entry.split_lo_complete))
                sob_entry.addr_will_complete = true;

            store_wakeup_dependent_loads(st_alloc_id, WakeCode::ST_ADDR);
        }
    }
}

void LSU::store_data_early_complete_stage()
{
    while (store_data_early_complete.can_take())
        inflight_store_data_complete.emplace_front(store_data_early_complete.take());
}

void LSU::store_data_execute(Uop uop)
{
    auto sob_iter = sob.find(uop->store_associated_sta_id);
    if (sob_iter == sob.end())
        return;

    auto& sob_entry = sob_iter->second;

    if (uop->is_store_data_high())
        sob_entry.data_high_complete = true;
    else
        sob_entry.data_low_complete = true;

    uop_std_execute_event(uop);

    if (uop->is_single_uop_store()) {
        if (sob_entry.wake_code == WakeCode::ST_DATA) {
            sob_entry.wakeup();

            if (sus_skip_final_sta && sta_only_needs_completion(sob_entry)) {
                uop_sta_early_complete_event(sob_entry.uop);

                AMPERE_ASSERT(!sob_entry.needs_writeback(), "Early completion of STA which actually needs writeback");
                sob_entry.must_recycle = false;
                sob_entry.recycle = false;
            }
        }

        if (sob_entry.is_ready_for_writeback()) {
            AMPERE_ASSERT(sob_entry.page_type != PageType::UNKNOWN, "Page type is unknown after address translation.");

            // if MTE is enabled and page is tagged, cannot write back to IXU until tag check is done
            if (sta_part_completed(sob_entry) && can_complete_exclusive_store(sob_entry)) {
                writeback_store(sob_entry, uop);
                try_completing_store(sob_entry, uop);
            }
        }
    } else {
        writeback_store(sob_entry, uop);
        complete_store(sob_entry, uop);
    }

    store_data_complete.put(uop);
    store_data_early_complete.put(uop->store_associated_sta_id);
}

void LSU::store_data_complete_stage()
{
    while (store_data_complete.can_take()) {
        auto uop = store_data_complete.take();
        auto sob_iter = sob.find(uop->store_associated_sta_id);
        if (sob_iter != sob.end() && sob_iter->second.st_data_load)
            store_wakeup_dependent_loads(uop->store_associated_sta_id, WakeCode::ST_DATA);

        inflight_store_data_complete.pop_back();
    }
}

bool LSU::can_complete_barrier(const SOBEntry& sob_entry) const
{
    return can_commit_barrier && is_non_speculative(sob_entry) && get_cycle() >= barrier_can_execute_cycle;
}

// return true if an STA can be scheduled to the store pipe
bool LSU::can_sta_schedule(SOBEntry& sob_entry) const
{
    if (!sob_entry.uop)
        return false;

    if ((stores_sched_pipe & sob_entry.pipe_mask())
        || (sta_fill_conflicts && l2c_refill_pipe_consumed(l2c_refill_store_arbitration, sob_entry.pipe_mask())))
        return false;

    auto now = get_cycle();
    auto bypass_cycle = sob_entry.issue_cycle + sta_bypass_delay;
    auto first_exec_cycle_after_bypass = bypass_cycle + 1 + delay_after_sob_bypass;

    // bypass_cycle is the earliest this store can execute
    if (now < bypass_cycle)
        return false;

    // If the store doesn't bypass, there may be a delay before it can schedule out of the SOB
    if (now != bypass_cycle && now < first_exec_cycle_after_bypass)
        return false;

    // We can't complete a DMB-all or DSB unless the L2 says it's OK
    // and it is at the head of the ROB. Note that the alloc_id of the barrier at
    // the head of the ROB is delayed by a couple cycles to give a store which
    // just committed time to get into the PWC and factor into the L2 signal.
    if (sob_entry.uop->is_barrier_dmb_all() || sob_entry.uop->is_barrier_dsb())
        if (!can_complete_barrier(sob_entry))
            return false;

    // On Furiosa, exclusive stores, atomic loads, and atomic stores wait until they are non-spec (next to commit).
    // From Ripley onward, these can be scheduled speculatively.
    if ((stores_with_dest_nonspec && sob_entry.uop->is_exclusive())
        || ((stores_with_dest_nonspec || (!atomics_in_l2 && !prefetch_atomics)) && sob_entry.uop->is_atomic_load())
        || ((atomic_stores_nonspec || (!atomics_in_l2 && !prefetch_atomics)) && sob_entry.uop->is_atomic_store()))
    {
        if (auto non_spec = sob.find(non_spec_uop_id);
            (non_spec == sob.end() || !is_next_part_of_wide_atomic_load(sob_entry.uop, non_spec->second.uop))
            && !is_non_speculative(sob_entry))
            return false;
    }

    return true;
}

// scan through the SOB to find schedulable uops for execution; stop when store slots are full
void LSU::sta_pipeline()
{
    for (auto& [_, sob_entry] : sob) {
        if (!sob_entry.is_ready() || !can_sta_schedule(sob_entry))
            continue;

        execute_sta(sob_entry);

        if (!can_store_pipeline_schedule())
            break;
    }
}

// execute an individual STA uop for address resolution
void LSU::execute_sta(SOBEntry& sob_entry)
{
    if (can_skip_sta(sob_entry)) {
        skip_sta_and_complete(sob_entry, false, true);
        return;
    }

    sob_entry.executing = true;
    uop_sta_execute_event(sob_entry.uop);
    stores_sched_pipe |= sob_entry.pipe_mask();
    stores_executed++;
    store_pipe_occupancy_event(sob_entry.uop,
                               pipe_partition(sob_entry.pipe_mask()),
                               pipe_occupant(sta_load_required(sob_entry) ? PipeOccupant::STA_LD : PipeOccupant::STA));
    if (sob_entry.issue_cycle + sta_bypass_delay == get_cycle())
        ev_store_bypass_from_ixu(sob_entry.uop);

    if (!sob_entry.uop->is_predicated_off()) {
        if (!sob_entry.writeback_only() && !dl1_tlb_access(sob_entry)) {
            sob_entry.recycle = true;
            DBG_LSU("{}: SOB recycling id: {}, txid: {} {:016x} {}",
                    get_cycle(),
                    sob_entry.uop->store_associated_sta_id,
                    sob_entry.uop->get_txid(),
                    sob_entry.uop->pc().value(),
                    sob_entry.uop->get_uopcode());

            st_exec_recycle_complete.put(sob_entry.uop->alloc_id);
            uop_st_recycle_event(sob_entry.uop, st_recycle_reason(RecycleReason::DTLB_MISS));
            return;
        } // DL1 TLB hit or faulted. Let the uop complete.

        // address translation: now we know whether page is tagged or not
        sob_entry.page_type = tagged_pages ? PageType::TAGGED : PageType::UNTAGGED;
        if (sob_entry.uop->is_store_barrier())
            sob_entry.page_type = PageType::NON_ADDR;
    } else {
        sob_entry.page_type = PageType::UNTAGGED;
    }

    // Don't clear must_recycle until we're really sure the store is going to the
    // complete stage -- otherwise when waking from TLB_MISS sleep it will not re-execute
    if (!sob_entry.uop->is_atomic_load() || !sob_entry.recycle)
        sob_entry.must_recycle = false;
    store_complete.put(sob_entry.uop->alloc_id);

    if (sta_early_wakeup)
        sta_wakeup.put({sob_entry.uop->alloc_id, true});
}

void LSU::store_wakeup_dependent_load(LOBEntry& lob_entry, uint64_t sob_alloc_id, WakeCode code)
{
    bool unspecific_wake = code == WakeCode::ST_DATA && !specific_st_data_wake;
    if (lob_entry.wake_code == code
        && (unspecific_wake || (lob_entry.uop->is_younger(sob_alloc_id) && *lob_entry.wake_id == sob_alloc_id)))
    {
        if (code == WakeCode::ST_DATA)
            uop_ld_dependent_on_std_wakeup_event(lob_entry.uop, sta_wakeup_ld_data(sob_alloc_id));
        else if (code == WakeCode::ST_ADDR)
            uop_ld_dependent_on_sta_wakeup_event(lob_entry.uop, sta_wakeup_ld_data(sob_alloc_id));
        else if (code == WakeCode::ST_RETIRE)
            uop_ld_dependent_on_stret_wakeup_event(lob_entry.uop, std_wakeup_ld_data(sob_alloc_id));
        lob_entry.wakeup();
    }
}

void LSU::store_wakeup_dependent_loads(uint64_t sob_alloc_id, WakeCode code)
{
    for (auto& [_, lob_entry] : lob)
        store_wakeup_dependent_load(lob_entry, sob_alloc_id, code);
}

void LSU::store_address_raw_scrub(const SOBEntry& sob_entry)
{
    if (!sob_entry.writes_cache())
        return;

    uint64_t sob_alloc_id = sob_entry.uop->alloc_id;
    PhysAddr sob_phys_addr = *sob_entry.uop->phys_addr;
    unsigned sob_type = sob_entry.uop->is_single_uop_store();
    auto sob_size = sob_entry.uop->mem_datasize();
    auto sob_match_addr = PhysAddr(sob_phys_addr.extract_range(27, 0));
    auto sob_match_size = raw_scrub_4b_mask_bug ? std::max(sob_size, 4u) : sob_size;

    for (auto& lob_handle : lob) {
        auto& lob_entry = lob_handle.second;

        // Load-acquire must go in order w.r.t. any older store-releases, so when a release executes any younger
        // acquires have to be scrubbed, regardless of address. On cores where acquires are not allowed to skip
        // unknown stores, this check is unnecessary but harmless, since any younger acquires will be sleeping.
        //
        // Check for younger acquires which are complete or in the pipe (got_l1_data or forwarded_from_store)
        if (sob_entry.uop->is_release() && lob_entry.uop->is_acquire() && lob_entry.uop->is_younger(sob_alloc_id)
            && (lob_entry.got_l1_data || lob_entry.forwarded_from_store || lob_entry.complete))
        {
            uop_ldar_raw_scrub(lob_entry.uop);
            lob_entry.uop->raw_scrubbed = true;
            raw_scrub_port->put(lob_entry.uop);
            sf_predictor.raw_scrub(lob_entry.uop);
            pmu_event(lob_entry.uop, pmu_num_data(archx::PMU::LSU_SFP_MISPRED_SKIP));
            return;
        }

        // RAW scrub conditions: the load is younger than this store, AND
        // the load has skipped at least one unknown store, AND
        // the load has walked back past this store (it skipped this store or an older one)
        // Software prefetches don't read data so they can't be scrubbed
        if (lob_entry.uop->is_younger(sob_alloc_id) && lob_entry.skipped_unknown_store
            && lob_entry.oldest_skipped_store <= sob_alloc_id)
        {
            AMPERE_ASSERT(lob_entry.can_forward(), "Doing raw scrub check on load which can't forward");

            bool match{false};
            auto lob_phys_addr = *lob_entry.uop->phys_addr;
            auto lob_size = lob_entry.uop->mem_datasize();

            // Wrong-path uops have zero addresses, so don't signal lots of bogus scrubs
            if (lob_phys_addr == PhysAddr(0) && sob_phys_addr == PhysAddr(0))
                continue;

            // Older cores check for RAW scrubs with a 4-byte granularity. If the address is not 4-byte aligned,
            // RTL says it accesses an additional 4-byte chunk. This works for 4/8/16-byte uops, but often treats
            // 1- and 2-byte uops as accessing an extra chunk which they do not, in fact, touch. The effect is the
            // same as treating 1- and 2-byte uops as having a 4-byte size: if they are 4-byte aligned they will
            // fit in one chunk, otherwise they will span two.
            auto lob_match_addr = PhysAddr(lob_phys_addr.extract_range(27, 0));
            auto lob_match_size = raw_scrub_4b_mask_bug ? std::max(lob_size, 4u) : lob_size;
            match = address_range_a_intersects_b(
                sob_match_addr, sob_match_size, lob_match_addr, lob_match_size, raw_scrub_granularity);

            if (match) {
                if (no_scrub_sleeping_load && lob_entry.wake_code) {
                    // If the load is asleep, then it hasn't actually given bad data to its dependents yet, and so
                    // it's not really necessary to flush -- we could just wake it up and re-execute, and this time
                    // it should forward from the older store.
                    lob_entry.oldest_skipped_store = ULLONG_MAX;
                    lob_entry.wakeup();
                    ev_raw_wakeup(lob_entry.uop);
                } else {
                    if (!lob_phys_addr.same_cache_line(sob_phys_addr))
                        ev_raw_scrub_false_hi(lob_entry.uop);
                    else if (!address_range_a_intersects_b(sob_phys_addr, sob_size, lob_phys_addr, lob_size, 1))
                        ev_raw_scrub_false_lo(lob_entry.uop);

                    if (!lob_entry.uop->raw_scrubbed) {
                        uop_ld_raw_scrub_detected_event(lob_entry.uop,
                                                        lob_entry.uop->pc(),
                                                        ld_address_data(lob_phys_addr.value()),
                                                        st_address_data(sob_phys_addr.value()),
                                                        ld_datasize_data(lob_size),
                                                        st_datasize_data(sob_size),
                                                        sfp_index_data(sf_predictor.compute_set(lob_entry.uop->pc())),
                                                        ld_raw_entry_data(lob_entry.uop->alloc_id),
                                                        sta_raw_entry_data(sob_alloc_id),
                                                        sta_txid(sob_entry.uop->get_txid()),
                                                        sta_raw_type_data(sob_type));
                    }

                    lob_entry.uop->raw_scrubbed = true;
                    raw_scrub_port->put(lob_entry.uop);

                    sf_predictor.raw_scrub(lob_entry.uop);

                    return;
                }
            }
        }
    }
}

void LSU::complete_store_recycle(In<uint64_t>& store_recycle_in, bool force)
{
    while (store_recycle_in->can_take()) {
        // Completing SOB entry may have been erased due to flush
        if (auto sob_iter = sob.find(store_recycle_in->take()); sob_iter != sob.end()) {
            auto& sob_entry = sob_iter->second;
            AMPERE_ASSERT(sob_entry.uop, "sob uop must exist. alloc_id {}", sob_iter->first);
            DBG_LSU("{}: SOB recycle end {} {:016x} {}",
                    get_cycle(),
                    sob_entry.uop->get_txid(),
                    sob_entry.uop->pc().value(),
                    sob_entry.uop->get_uopcode());

            if (force || !sus_skip_final_sta || !sob_entry.uop->is_single_uop_store() || !sob_entry.addr_complete
                || sob_entry.recycle)
                sob_entry.recycle_into_scheduler();
        }
    }
}

void LSU::store_complete_stage()
{
    // Scenarios in which STA-load may have finished:
    //  1) STA-load got scheduled and experienced DL1 hit.
    //  2) This STA is executing even after tag check is done. Scenario: still waiting for SUS DATA.

    // Scenarios in which STA-load may not have finished yet:
    //  1) STA-load did not get scheduled in the load pipe (there was some older load).
    //  2) STA-load got scheduled but encountered DL1 miss or full OFB.

    while (store_complete.can_take()) {
        // Completing SOB entry may have been erased due to flush
        auto sob_iter = sob.find(store_complete.take());
        if (sob_iter == sob.end())
            continue;

        auto& sob_entry = sob_iter->second;

        AMPERE_ASSERT(sob_entry.page_type != PageType::UNKNOWN, "Page type is unknown after address translation.");
        // can only mean that tagged StaLd is in-flight
        AMPERE_ASSERT(!sob_entry.recycle || sta_load_allowed(sob_entry),
                      "Sob entry is recycling (indicating in-flight StaLd) even if MTE is disabled or StaLd is "
                      "non-tagged or StaLd otherwise not allowed");

        // Store barriers and untagged pages do not require tag check
        if (sob_entry.split) {
            if (!sob_entry.split_lo_complete) {
                if (sob_entry.recycle) {
                    // If MTE is enabled, it is possible that this SOB entry was co-scheduled down load and store pipes.
                    // The uop may already have been put to load recycle port, and is still recyling (e.g., recycling
                    // due to L1_MISS). So, no need to double recycle through store recycle pipe. However, need to
                    // explicitly reset 'executing' here because STA-load recycle completion does not do it (for various
                    // valid reasons). Not resetting 'executing' here will therefore never again make this uop eligible
                    // to schedule. Note: from now until the STA-load recycle completion, the execution of this uop is
                    // prevented by the 'recycle' flag being on.
                    sob_entry.executing = false;
                    continue;
                }

                // no need to set must_recycle because addr_complete is not set, so it will re-execute
                recycle(sob_entry, st_recycle_complete.out);

                // cannot move to high half until tag check on low half is done.
                if (sta_part_completed(sob_entry, true)) {
                    // Mark the low half complete and send the uop back through the pipeline
                    adjust_sta_for_split(sob_entry);
                } else {
                    uop_st_recycle_event(sob_entry.uop, st_recycle_reason(RecycleReason::SPLIT_LO_TAG_CHECK_NOT_DONE));
                }
                continue;
            } else {
                // For MTE: Do not restore addresses if high half's sta-load is not done yet.
                // But still move on to marking addr_complete as true so that critical ops depending on ST_ADDR can be
                // performed.
                if (sta_part_completed(sob_entry)) {
                    sob_entry.restore_base_addr();
                }
            }
        }

        sob_entry.executing = false;

        if (!sob_entry.addr_complete) {
            // addr_complete may already be true here for two reasons:
            //  1) MTE is enabled; STA re-executed due to co-issue with STA-load.
            //  2) STA executed while waiting for SUS DATA.
            sob_entry.addr_complete = true;
            if (!sta_early_wakeup)
                store_wakeup_dependent_loads(sob_entry.uop->alloc_id, WakeCode::ST_ADDR);
            store_address_raw_scrub(sob_entry);
        }

        if ((sob_entry.uop->is_single_uop_store() && sob_entry.fat_sus) || sob_entry.uop->is_sw_prefetch()) {
            sob_entry.data_low_complete = true;
            if (sob_entry.st_data_load)
                store_wakeup_dependent_loads(sob_entry.uop->alloc_id, WakeCode::ST_DATA);
        }

        bool recycled_due_to_in_flight_load = false;

        if (!sob_entry.uop->is_sw_prefetch()) {
            // Check whether this store needs to recycle because there was an in-flight load which it might have
            // failed to RAW scrub. Only loads which skipped an unknown store will be in this list.
            for (auto ld = loads_in_raw_scrub_window.begin(); ld != loads_in_raw_scrub_window.end();) {
                // If the load is old enough we don't have to worry about it
                if (get_cycle() - ld->second >= in_flight_raw_scrub_window) {
                    ld = loads_in_raw_scrub_window.erase(ld);
                    continue;
                }

                // If VA[11:6] match between store and load, then the store must recycle.
                if (ld->first.extract_range(11, 6) == sob_entry.uop->virt_addr->extract_range(11, 6)) {
                    // If SOB entry is already recycling due to the co-issued STA-load, just set must_recycle to ensure
                    // re-execution.
                    sob_entry.must_recycle = true;
                    if (!sob_entry.recycle /*sta-load is not in load pipe somewhere*/) {
                        recycle_store(sob_entry, RecycleReason::IN_FLIGHT_LOAD);
                        recycled_due_to_in_flight_load = true;
                    }
                    break;
                }

                ++ld;
            }
        }

        if (recycled_due_to_in_flight_load)
            continue;

        if (!sob_entry.recycle /* STA(-load) is not in the load pipe somewhere*/) {
            if (sta_part_completed(sob_entry)) {
                if (!try_completing_sta(sob_entry, false))
                    continue;
            } else {
                // STA-load could not be scheduled along with this STA due to unavailable load pipes.
                // This STA must re-execute to accompany the STA-load.
                // However, re-execution isn't possible unless the recycle pipeline has been traversed.
                auto reason = speculative_atomic_load_allowed(sob_entry)
                                  ? RecycleReason::SPEC_ATOMIC
                                  : (sob_entry.tag_check_required() ? RecycleReason::TAG_CHECK_NOT_DONE
                                                                    : RecycleReason::IN_FLIGHT_LOAD);
                recycle_store(sob_entry, reason);
            }
        }
    }

    // Set stores marked to recycle as now eligible to reschedule
    complete_store_recycle(st_recycle_complete.in);
    complete_store_recycle(st_exec_recycle_complete.in, true);
}

void LSU::store_delayed_completion_stage()
{
    while (store_delayed_completion.can_take()) {
        auto alloc_id = store_delayed_completion.take();
        if (auto sob_iter = sob.find(alloc_id); sob_iter != sob.end())
            sob_iter->second.executing = false;
    }

    if (sob.empty())
        return;

    uint8_t scheduled = 0;
    for (const auto& [alloc_id, sob_entry] : sob) {
        if (sta_needs_schedule_for_completion(sob_entry) && !(scheduled & sob_entry.pipe_mask())) {
            pending_store_completions.emplace(alloc_id, false);
            scheduled |= sob_entry.pipe_mask();

            if (scheduled == ((1U << sta_pipes()) - 1))
                break;
        }
    }
}

void LSU::complete_pending_stores()
{
    uint8_t completed = 0;
    for (auto [alloc_id, from_pipeline] : pending_store_completions) {
        if (auto sob_iter = sob.find(alloc_id); sob_iter != sob.end()) {
            auto& sob_entry = sob_iter->second;
            if (completed & sob_entry.pipe_mask()) {
                if (all_deferred_store_completion_requires_delay || from_pipeline) {
                    sob_entry.executing = true;
                    store_delayed_completion.put(alloc_id);
                }
                uop_sta_delayed_complete_event(sob_entry.uop);
                continue;
            }

            bool also_writeback = sta_only_completes(sob_entry);
            complete_store(sob_entry, sob_entry.uop, also_writeback);
            completed |= sob_entry.pipe_mask();
        }
    }

    pending_store_completions.clear();
}

bool LSU::try_completing_sta(SOBEntry& sob_entry, bool refill, bool as_load)
{
    if (sob_entry.uop->is_single_uop_store() && !sob_entry.fat_sus && !sob_entry.data_low_complete) {
        if (!enable_sus_recycle)
            sob_entry.go_to_sleep(WakeCode::ST_DATA);
        recycle_store(sob_entry, RecycleReason::SUS_DATA_NOT_READY, true);
        return false;
    }

    // SOB entry may have been required to recycle due to its failure to RAW scrub an in-flight load.
    if (refill && sob_entry.must_recycle) {
        recycle_store(sob_entry, RecycleReason::IN_FLIGHT_LOAD);
        return false;
    }

    complete_sta(sob_entry, true, as_load);
    return true;
}

bool LSU::can_skip_sta(const SOBEntry& sob_entry) const
{
    return ((dmb_all_skip_final_sta && sob_entry.uop->is_barrier_dmb_all())
            || (dsb_skip_final_sta && sob_entry.uop->is_barrier_dsb()))
           && can_complete_barrier(sob_entry);
}

bool LSU::can_skip_sta_at_issue(const SOBEntry& sob_entry) const
{
    return (instantly_complete_uops_not_needing_sta && !sob_entry.needs_sta_execute()) || can_skip_sta(sob_entry);
}

bool LSU::sta_only_completes(const SOBEntry& sob_entry) const
{
    return !sob_entry.wrote_back && sob_entry.is_execution_complete() && can_skip_sta(sob_entry);
}

void LSU::try_completing_sta_at_issue(SOBEntry& sob_entry)
{
    if (can_skip_sta_at_issue(sob_entry))
        skip_sta_and_complete(sob_entry);
}

void LSU::adjust_sta_for_split(SOBEntry& sob_entry)
{
    sob_entry.adjust_to_hi_half_addr();

    uop_st_recycle_event(sob_entry.uop, st_recycle_reason(RecycleReason::SPLIT));
    uop_st_cacheline_split_event(sob_entry.uop);
    // LDST_ALIGN_LAT is computed in rollup_formula.py as LD_ALIGN_LAT + ST_ALIGN_LAT
    pmu_event(sob_entry.uop, pmu_num_data(archx::PMU::ST_ALIGN_LAT));
}

bool LSU::is_combinable_store(const SOBEntry& sob_entry) const
{
    const auto& uop = sob_entry.uop;
    // Small window between SOB alloc and writing the uop -- just assume it will turn out to be combinable
    if (!uop)
        return true;

    return (!sob_entry.split && !sob_entry.ordered && !sob_entry.barrier && !next_store_is_ordered
            && !uop->is_store_special() && !uop->is_predicated_off()
            && // predicated off uops do not have physical addresses
            sob_entry.wake_code != WakeCode::RETRY_FAULT);
}

bool LSU::st_retire_load_exists() const
{
    // Loads in ST_RETIRE, RETRY_FAULT, and STLR sleep states may not be able to make progress until an older store
    // retires.
    for (const auto& lob_handle : lob) {
        const auto& lob_entry = lob_handle.second;
        if (lob_entry.wake_code == WakeCode::ST_RETIRE || lob_entry.wake_code == WakeCode::RETRY_FAULT
            || lob_entry.wake_code == WakeCode::STLR)
            return true;
    }

    return false;
}

// LSU will not delay a store's retirement if the next SOB entry is not even valid yet
bool LSU::next_store_has_issued(uint64_t oldest_store_id) const
{
    auto oldest_store = preview_rob.find(oldest_store_id);
    AMPERE_ASSERT(oldest_store != preview_rob.end(), "Could not find oldest store in preview ROB");

    auto next_store = ++oldest_store;
    if (next_store == preview_rob.end())
        return false;
    auto next_store_in_sob = sob.find(next_store->second->alloc_id);
    return next_store_in_sob != sob.end();
}

void LSU::atomic_load_receives_data(SOBEntry& sob_entry)
{
    sob_entry.exclusive_receives_data(true);
    try_completing_sta_load(sob_entry, true);

    if (!atomic_load_speculative)
        return;

    if (!sob_entry.is_casp_hi()) {
        if (speculative_atomic_load_cooldown) {
            --speculative_atomic_load_cooldown;
        } else if (sob_entry.wrote_back) {
            ++atomic_loads_speculated;
            if (atomic_load_speculative_abort_every
                && atomic_loads_speculated % atomic_load_speculative_abort_every == 0)
            {
                if (++atomic_loads_wrong >= atomic_load_speculative_cooldown_after) {
                    atomic_loads_wrong = 0;
                    speculative_atomic_load_cooldown = atomic_load_speculative_cooldown_duration;
                }

                uop_ldar_raw_scrub(sob_entry.uop);
                sob_entry.uop->raw_scrubbed = true;
                raw_scrub_port->put(sob_entry.uop);
            }
        }
    }
}

LSU::SOB::iterator LSU::next_store_to_retire()
{
    if (sob.empty())
        return sob.end();

    // For committed stores, the 'executing' flag means they are already in the retirement pipe. Start
    // by finding the oldest store which is not already retiring.
    auto sob_iter = sob.begin();
    for (; sob_iter != sob.end() && sob_iter->second.retiring()
           && (!sob_iter->second.split || sob_iter->second.split_lo_retire_schedule_done
               || sob_iter->second.is_going_to_dealloc);
         ++sob_iter)
    {}

    return sob_iter;
}

bool LSU::store_retirement_schedule(Pipeline<StoreRetirementOps>::Data ops)
{
    auto sob_iter = next_store_to_retire();
    // For exclusives, we allow them to retire the first time through but not the second time, which is just to write
    // back.
    if (sob_iter == sob.end() || sob_iter->second.exclusive_is_ready_to_complete())
        return false;

    bool non_speculative = is_non_speculative(sob_iter->second);
    if (is_start_of_wide_atomic_load(sob_iter->second.uop) && non_speculative
        && sob_iter->second.exclusive_is_retiring())
    {
        for (auto next_oldest_sob_iter = std::next(sob_iter);
             next_oldest_sob_iter != sob.end()
             && is_next_part_of_wide_atomic_load(next_oldest_sob_iter->second.uop, sob_iter->second.uop);
             ++next_oldest_sob_iter)
        {
            if (next_oldest_sob_iter->second.is_following_part_of_wide_atomic_load(sob_iter->second.uop)
                && next_oldest_sob_iter->second.exclusive_is_not_retiring())
            {
                sob_iter = next_oldest_sob_iter;
                break;
            }
        }
    }

    auto& oldest_store = sob_iter->second;

    // Retire the oldest store if is committed, or it is an exclusive/atomic which has executed and is not already
    // retiring. If it is an exclusive/atomic that was allowed to execute speculatively, then it must also be next to
    // commit.
    bool should_retire = oldest_store.committed
                         || (oldest_store.exclusive_complete && !oldest_store.executing
                             && (stores_with_dest_nonspec || (!stores_with_dest_nonspec && non_speculative)));

    // Untagged store uops don't require tag check.
    // In addition, store barriers don't require tag check. However, store barriers never reach here.
    AMPERE_ASSERT(!enable_mte || !should_retire || oldest_store.tag_check_complete(),
                  "Scheduling a store retirement but tag check not yet complete");

    if (!should_retire)
        return false;

    unsigned stores_to_delay = 0;
    unsigned total_bytes = sob_iter->second.uop->is_sw_prefetch() ? 0 : sob_iter->second.uop->mem_datasize();
    std::vector<decltype(sob_iter)> combined_stores;
    for (unsigned i = 0; i < combine_stores; ++i) {
        // If the oldest store is not the last thing in the SOB, and both this and the next store are combinable,
        // and either we don't know the PA of the second store or the stores are to the same line, then we might
        // be combining them. SW prefetches can always combine.
        auto& current_store = sob_iter->second;

        if (!next_store_has_issued(current_store.uop->alloc_id))
            break;

        auto& next_store = (++sob_iter)->second;
        bool both_combinable = is_combinable_store(current_store) && is_combinable_store(next_store);
        bool one_sw_prefetch =
            current_store.uop->is_sw_prefetch() || (next_store.uop && next_store.uop->is_sw_prefetch());
        bool maybe_same_line =
            !next_store.addr_complete || current_store.uop->phys_addr->same_cache_line(*next_store.uop->phys_addr);
        bool only_combine_with_prefetch = total_bytes == store_retirement_width;
        if (next_store.uop && !next_store.uop->is_sw_prefetch())
            total_bytes += next_store.uop->mem_datasize();

        bool might_combine =
            both_combinable && (maybe_same_line || one_sw_prefetch) && total_bytes <= store_retirement_width;

        // If this store might combine with the next one, but the next one is not yet committed, and there
        // is no load waiting for a store to retire, then hold off on retiring to see if we can combine later.
        // But we never delay retirement of a SW prefetch, since it's not sending data to L2; also we won't delay
        // this store if the next one is a prefetch, or if the only thing this one could combine with is a prefetch.
        if (delayed_retirement && might_combine && (stores_to_delay || !next_store.committed)
            && !st_retire_load_exists() && !one_sw_prefetch && !only_combine_with_prefetch)
            stores_to_delay = i + 1;

        if (!might_combine || stores_to_delay >= delayed_retirement)
            break;

        combined_stores.emplace_back(sob_iter);
    }

    if (stores_to_delay > 0) {
        delayed_stores = stores_to_delay;
        uop_st_retire_delay_event(oldest_store.uop);
        return false;
    }

    // Now we are definitely retiring the oldest store
    bool hi_half = false;
    if (oldest_store.split && oldest_store.executing)
        hi_half = oldest_store.split_lo_retire_schedule_done = true;

    oldest_store.executing = true;
    uop_st_retire_schedule_event(oldest_store.uop);

    bool write_through = !store_write_to_l2_on_miss || write_through_required();
    ops = StoreRetirementOps(oldest_store, local_atomic(oldest_store.uop), write_through, hi_half);

    for (auto combined : combined_stores) {
        auto& combined_store = combined->second;
        // If nothing prevents combining, and the second store is ready to retire, then combine them.
        if (combined_store.committed) {
            uop_st_retire_combine_event(combined_store.uop);

            combined_store.executing = true;
            uop_st_retire_schedule_event(combined_store.uop);
            ops->add(combined_store, local_atomic(combined_store.uop), write_through);
        }
    }

    // We just sent the oldest store down the retirement pipe, so it's not waiting to combine
    delayed_stores = 0;

    return true;
}

void LSU::StoreRetirementOps::add(const SOBEntry& store, bool local, bool default_write_through, bool hi_half)
{
    const auto& part = ops.emplace_back(store, hi_half);
    const auto& uop = store.uop;

    // Note that the barriers are checked in order from strictest to weakest, so that we never send a weaker
    // barrier type than is necessary. Maybe this would be better as a bit vector of atomic/excl/release/ordered...
    if (uop->is_release() && uop->is_exclusive())
        barrier_type = MemOp::BarrierType::BAR_STORE_EXCL_RELEASE;
    else if (uop->is_release())
        barrier_type = MemOp::BarrierType::BAR_STORE_RELEASE;
    else if (uop->is_exclusive())
        barrier_type = MemOp::BarrierType::BAR_STORE_EXCL;
    else if (store.ordered)
        barrier_type = MemOp::BarrierType::BAR_DMB;

    if (uop->is_atomic_load())
        atomic_type = is_wide_atomic_load(uop) ? MemOp::AtomicType::ATOMIC_LOAD_WIDE : MemOp::AtomicType::ATOMIC_LOAD;
    else if (uop->is_atomic_store())
        atomic_type = MemOp::AtomicType::ATOMIC_STORE;

    datasize = part.datasize(uop);
    page_size = store.page_size;
    bytemask |= part.bytemask(uop);
    non_temporal = uop->is_nontemporal();
    write_through |= default_write_through && store.writes_cache();
    write_through |= !local && (uop->is_atomic() || barrier_type != MemOp::BarrierType::BAR_NONE);
}

PhysAddr LSU::StoreRetirementOps::Part::phys_addr(const Uop& uop) const
{
    return hi_half ? uop->phys_addr->next_line_address() : *uop->phys_addr;
}

VirtAddr LSU::StoreRetirementOps::Part::virt_addr(const Uop& uop) const
{
    return hi_half ? uop->virt_addr->next_line_address() : *uop->virt_addr;
}

unsigned int LSU::StoreRetirementOps::Part::datasize(const Uop& uop) const
{
    return hi_half ? (*uop->virt_addr + uop->mem_datasize()) - uop->virt_addr->next_line_address()
                   : uop->mem_datasize();
}

uint64_t LSU::StoreRetirementOps::Part::bytemask(const Uop& uop) const
{
    return phys_addr(uop).line_bytemask(datasize(uop));
}

MemReq LSU::create_store_request(const StoreRetirementOps& req, Uop l2_uop, const StoreRetirementOps::Part& l2_part)
{
    return make_child<MemReq>(l2_uop,
                              MemReqObj::store(l2_uop->ainst()->inum().id(),
                                               l2_uop->pc(),
                                               l2_part.phys_addr(l2_uop),
                                               l2_part.virt_addr(l2_uop),
                                               req.datasize,
                                               req.bytemask,
                                               req.page_size,
                                               req.barrier_type,
                                               req.atomic_type,
                                               enable_nontemporals && l2_uop->is_nontemporal()));
}

// Send L2 write request
//   - Store request will not expect response, req id = 0
// Atomic loads behave almost exactly like an exclusive store-release, so we can just reuse that for now.
// FIXME: eventually L2 needs to know that this is an atomic to have the correct cache fill behavior
void LSU::send_l2_store_miss_request(const StoreRetirementOps& req, Uop l2_uop, const StoreRetirementOps::Part& l2_part)
{
    l2_store_miss_request->put(create_store_request(req, l2_uop, l2_part));

    // MEM_ACCESS is computed in rollup_formula.py as MEM_ACCESS_RD + MEM_ACCESS_WR
    pmu_event(l2_uop, pmu_num_data(archx::PMU::MEM_ACCESS_WR));
}

bool LSU::can_evict_to_l2() const
{
    return store_write_to_l2_on_miss
           && (evict_modified_via_l2_pwc ? l2_store_miss_request->can_put() : l2_writeback_request->can_put());
}

void LSU::send_l2_evict_request(Txn tx, const L1DCacheline& evicted)
{
    AMPERE_ASSERT(store_write_to_l2_on_miss, "Data should never be modified in L1d");
    auto mem_req = make_child<MemReq>(tx, MemReqObj::eviction(0, evicted.addr, MemOp::Requester::LSU));

    bool should_wait = false;
    if (!pwc && evict_modified_via_l2_pwc) {
        auto sob_iter = next_store_to_retire();
        should_wait = sob_iter != sob.end() && is_wide_atomic_load(sob_iter->second.uop);
    }

    if (!should_wait && can_evict_to_l2()) {
        evict(mem_req);

        // MEM_ACCESS is computed in rollup_formula.py as MEM_ACCESS_RD + MEM_ACCESS_WR
        pmu_event(tx, pmu_num_data(archx::PMU::MEM_ACCESS_WR));
        ev_inval_modified(tx);
    } else {
        evictions.emplace_back(std::move(mem_req));
        ev_inval_modified_no_l2_write(tx);
    }
}

std::optional<StoreStallReason> LSU::check_store_retirement_stalls(VirtAddr virt_addr, bool write_through)
{
    // If refill in LF03, data port conflict with ST ret
    // LSU_ST_RETIRE_STALL is computed in rollup_formula.py as uop_st_retire_stall_event
    if ((l2c_refill_load_arbitration.can_take() && l2c_refill_load_arbitration.peek().guaranteed())
        || (l1d_cache_conflicts.evictions_cause_conflicts() && l2c_refill_evict_arbitration.can_take()
            && l2c_refill_evict_arbitration.peek().guaranteed()))
    {
        return StoreStallReason::DATA;
    }

    // L2DC partial write combiner full, stall
    if (write_through && !l2_store_miss_request->can_put())
        return StoreStallReason::L2;

    // Store-retire must stall for incoming fill to same line to complete write to L1D
    for (const auto& fill_addr : l2c_refill_inflight_addresses) {
        // FIXME: I think this should be checking set address, verify with Jon & Jason
        if (fill_addr.same_cache_line(virt_addr))
            return StoreStallReason::LINE;
    }

    return {};
}

bool LSU::store_retirement_stall(Pipeline<StoreRetirementOps>::Data ops)
{
    Uop l2_uop = nullptr;

    for (const auto& part : ops->ops) {
        auto sob_iter = sob.find(part.alloc_id);
        AMPERE_ASSERT_ERROR(sob_iter != sob.end(), "Could not find retiring store ID {} in SOB", part.alloc_id);

        const auto& sob_entry = sob_iter->second;
        const auto& uop = sob_entry.uop;

        if (!sob_entry.writes_cache())
            continue;

        if (!pwc || (write_through_skip_pwc && !ops->write_back() && write_through_exclusive_like(uop))) {
            if (auto reason = check_store_retirement_stalls(part.virt_addr(uop), ops->write_through); reason) {
                uop_st_retire_stall_event(uop, st_retire_stall_cause_data(*reason));
                return false;
            }
        }

        l2_uop = uop;
    }

    const auto& l2_part = ops->ops.back();
    if (pwc && (!write_through_skip_pwc || ops->write_back() || !write_through_exclusive_like(l2_uop))) {
        auto req = create_store_request(ops.get(), l2_uop, l2_part);
        if (!pwc->try_combine(req, ops->write_through)) {
            if (pwc->full()) {
                uop_st_retire_stall_event(req, st_retire_stall_cause_data(StoreStallReason::PWC));
                return false;
            }

            pwc->allocate(req, ops->write_through, req->atomic_type == MemOp::ATOMIC_LOAD_WIDE);
        }
    } else if (ops->write_through) {
        send_l2_store_miss_request(ops.get(), l2_uop, l2_part);
    }

    return true;
}

void LSU::store_retirement_write_cache(Txn tx, VirtAddr virt_addr, unsigned data_size)
{
    // This store hit so it will write the L1
    // L1D_CACHE is computed in rollup_formula.py as L1D_CACHE_RW + L1D_CACHE_PRFM
    // L1D_CACHE_RW is computed in rollup_formula.py as L1D_CACHE_RD + L1D_CACHE_WR
    pmu_event(tx, pmu_num_data(archx::PMU::L1D_CACHE_WR));

    // Communicate store retirement addresses to load_pipeline for bank conflict check,
    // but only on a hit -- if the store misses the L1 then there's no write and no conflict.
    //
    // FIXME: should also invalidate all VA synonyms of the store address from the L1
    l1d_cache_conflicts.retired_store_wrote_cache(virt_addr, data_size);
}

bool LSU::store_retirement_conflict(Pipeline<StoreRetirementOps>::Data ops)
{
    std::optional<decltype(l1d_cache)::Result> l1d_hit;
    Uop writeback_l2_uop = nullptr;
    std::optional<StoreRetirementOps::Part> l2_part;

    if (pwc)
        return true;

    for (const auto& part : ops->ops) {
        auto sob_iter = sob.find(part.alloc_id);
        if (sob_iter == sob.end())
            continue;

        auto& sob_entry = sob_iter->second;
        auto& uop = sob_entry.uop;

        if (!sob_entry.writes_cache())
            continue;

        // Check whether this store is going to hit the L1, but don't update LRU or produce any events
        // to avoid duplicating the events which will be generated when store retirement completes.
        // Atomics never hit the L1
        if (!write_through_atomic(uop)) {
            if (!l1d_hit) {
                l1d_hit = l1d_cache.lookup(uop,
                                           {part.virt_addr(uop), part.phys_addr(uop)},
                                           !(enable_nontemporals && ops->non_temporal) /* update LRU */,
                                           false,
                                           false,
                                           part.phys_addr(uop).value());
            }

            if (*l1d_hit) {
                // Require write miss-request if not unique in L1d
                if (ops->write_back() && !(*l1d_hit)->unique) {
                    if (write_allocate) {
                        if (l1d_cache_write_miss(ops.get(), part, sob_entry))
                            ev_cache_require_unique(uop);
                        return false;
                    } else {
                        if (!writeback_l2_uop)
                            ev_cache_require_unique(uop);
                        writeback_l2_uop = uop;
                        l2_part = part;
                    }
                } else {
                    store_retirement_write_cache(uop, part.virt_addr(uop), part.datasize(uop));
                }

                continue;
            } else {
                AMPERE_ASSERT(!local_atomic(uop), "Local atomic did not hit the L1d");
            }
        }

        if (ops->write_back()) {
            if (write_allocate) {
                if (l1d_cache_write_miss(ops.get(), part, sob_entry))
                    ev_cache_write_miss(uop);
                return false;
            } else {
                writeback_l2_uop = uop;
                l2_part = part;
            }
        }

        pmu_event(uop, pmu_num_data(archx::PMU::L1D_CACHE_MISS));
    }

    if (writeback_l2_uop) {
        if (!l2_store_miss_request->can_put()) {
            uop_st_retire_stall_event(writeback_l2_uop, st_retire_stall_cause_data(StoreStallReason::L2));
            return false;
        }

        send_l2_store_miss_request(ops.get(), writeback_l2_uop, *l2_part);
    }

    return true;
}

bool LSU::store_retirement_execute(Pipeline<StoreRetirementOps>::Data ops)
{
    for (const auto& part : ops->ops) {
        auto sob_iter = sob.find(part.alloc_id);
        if (sob_iter == sob.end())
            continue;

        AMPERE_ASSERT_ERROR(!sob_iter->second.is_going_to_dealloc,
                            "LSU attempting to retire a store with store ID {} in SOB that already retired",
                            part.alloc_id);

        bool oldest_check = true;
        for (auto sob_handle = sob.begin(); sob_handle != sob_iter; sob_handle++) {
            if (!sob_handle->second.is_going_to_dealloc)
                oldest_check = false;
        }
        AMPERE_ASSERT(sob_iter == sob.begin() || oldest_check
                          || (is_wide_atomic_load(sob_iter->second.uop) && sob_iter->second.exclusive_complete
                              && !sob_iter->second.exclusive_retired),
                      "LSU attempting to retire a store PC {} opcode {} that is not oldest.",
                      sob_iter->second.uop->pc(),
                      sob_iter->second.uop->get_uopcode());

        auto& sob_entry = sob_iter->second;
        auto& uop = sob_entry.uop;

        uop_st_retire_complete_event(uop, uop->virt_addr.value(), uop->phys_addr->line_address());

        if (!sob_entry.writes_cache()) {
            return_sob_credit(sob_iter, false);
            sob_dealloc.put(sob_iter);
            continue;
        }

        l1d_cache_conflicts.store_retired(part.virt_addr(uop), part.datasize(uop));

        // Atomic stores which hit in the L1 invalidate the line instead of updating it
        bool exclusive_write_through = write_through_exclusive_like(sob_entry.uop);

        // Write to cache (if present)
        if (!pwc && !exclusive_write_through) {
            auto result = l1d_cache.lookup(uop, {part.virt_addr(uop), part.phys_addr(uop)}, false, true, false);
            if (result && ops->write_back() && sob_entry.writes_cache()) {
                if (result->modified)
                    ev_cache_write_modified(uop);
                result->modified = true;
                l1d_cache.update_value(uop, {part.virt_addr(uop), part.phys_addr(uop)}, *result, true);
            }
        }

        // Invalidate aliases (and/or self for atomics)
        if (!pwc)
            l1d_cache_invalidate_aliases(uop, part.virt_addr(uop), part.phys_addr(uop), !exclusive_write_through);

        if (!sob_entry.split || part.hi_half) {
            // Deallocate the SOB entry, unless this is an exclusive in which case it needs to hang around
            // until the uop writes back its result.
            if (sob_entry.exclusive_complete && write_through_exclusive_like(sob_entry.uop)) {
                sob_entry.exclusive_retired = true;
            } else {
                if (local_atomic(sob_entry.uop)) {
                    AMPERE_ASSERT(sob_entry.exclusive_complete, "Atomic was not complete");
                    sob_entry.exclusive_second_pass = sob_entry.exclusive_retired = true;
                }
                if (acquire_wake_waits_till_older_retire_stlrs && uop->is_release()) {
                    AMPERE_ASSERT(retire_pending_store_releases, "Store releases already zero");
                    --retire_pending_store_releases;
                }
                return_sob_credit(sob_iter, false);
                sob_dealloc.put(sob_iter);
            }
        }
    }

    return true;
}

LSU::SOB::iterator LSU::delete_sob_entry(SOB::iterator sob_iter, bool is_flush)
{
    auto& uop = sob_iter->second.uop;
    if (!is_flush)
        preview_rob.erase(uop->alloc_id);
    sob_deallocate_event(uop);
    DBG_LSU(
        "delete in sob pc 0x{:x} {}, with sta {}", uop->pc().value(), uop->get_uopcode(), uop->store_associated_sta_id);
    return sob.erase(sob_iter);
}

void LSU::return_sob_credit(SOB::iterator sob_iter, bool is_flush)
{
    auto& uop = sob_iter->second.uop;

    DBG_LSU("return sob credit pc 0x{:x} {}, with sta {}",
            uop->pc().value(),
            uop->get_uopcode(),
            uop->store_associated_sta_id);
    if (sob_iter->second.barrier) {
        if (--sob_barrier_count == 0)
            lsu_has_barrier_out->put(false);
        AMPERE_ASSERT(sob_barrier_count >= 0, "SOB barrier count went negative!");
    }
    sob_iter->second.is_going_to_dealloc = true;
    // If this SOB entry is being cleared out by a flush, GPC has returned its credit and the preview_rob
    // is handled separately in the flush code.
    if (!is_flush) {
        credit_return->put(std::move(uop->sobid));

        // Wake up any loads which were waiting on this store to retire
        stret_wakeup.put(uop->alloc_id);
    }
}

void LSU::sob_deallocate_stage()
{
    while (sob_dealloc.can_take())
        delete_sob_entry(sob_dealloc.take(), false);
}

void LSU::gpc_store_commit_response()
{
    if (!stores_committed_port->can_take())
        return;

    auto stores_committed = stores_committed_port->take();

    // Mark non-committed SOB entries as committed and eligible for retirement based on GPC feedback
    for (auto sob_handle = sob.begin(); sob_handle != sob.end() && stores_committed > 0;) {
        auto& sob_entry = sob_handle->second;

        if (!sob_entry.committed) {
            sob_entry.committed = true;
            stores_committed--;

            // If the previous committed store was a DMB-store, mark this store as 'ordered'
            // to tell L2 not to schedule past it. Also, atomics are always ordered stores. Setting the
            // ordered bit doesn't matter for atomic loads since they go to the L2 before commit
            // (like an exclusive), but this has an effect for atomic stores.
            if (sob_entry.writes_cache()) {
                sob_entry.ordered = next_store_is_ordered || sob_entry.uop->is_atomic();
                next_store_is_ordered = false;
            }

            if (!sob_entry.uop) { // just to prevent a segfault below
                print_sob(std::cout);
                AMPERE_ERROR("GPC indicated commit on a SOB entry but it doesn't have a uop");
            }

            // untagged store uop addr and store barriers do not require tag check
            AMPERE_ASSERT(!enable_mte || sob_entry.tag_check_complete(),
                          "GPC indicates committed store but tag check not yet complete");

            for (const auto& inst : sob_entry.uop->instructions) {
                if (inst->archx_instruction->is_tlbi()) {
                    auto tlbi = make_child<TLBIReq>(sob_entry.uop, sob_entry.uop->alloc_id, inst);
                    tlbi_request_out->put(tlbi);
                    tlbi_immu_request_out->put(tlbi);
                }
            }

            // None of the barriers (DMB store, DMB all, DSB) actually live beyond commit. DMB all
            // and DSB have held up commit until their conditions were met, so they are done
            // immediately. DMB store will tag the next store as ordered. In all cases, just reclaim
            // the ROB/SOB entries and don't send anything down the store retirement pipeline.
            //
            // When an exclusive commits, deallocate its SOB entry since that wasn't done when it originally
            // retired. This will also wake up any loads that are waiting on it to cover a rare case when a
            // load went to sleep on this store while it was going through the pipe a second time.
            if (sob_entry.uop->is_store_barrier() || sob_entry.uop->is_exclusive() || sob_entry.uop->is_atomic_load()) {
                if (sob_entry.uop->is_barrier_dmb_st())
                    next_store_is_ordered = true;

                // HERE we do both
                return_sob_credit(sob_handle, false);
                sob_handle = delete_sob_entry(sob_handle, false);
                continue;
            }

            if (acquire_wake_waits_till_older_retire_stlrs && sob_entry.uop->is_release()
                && !sob_entry.uop->is_predicated_off())
                ++retire_pending_store_releases;
        }

        ++sob_handle;
    }
}

void LSU::gpc_load_commit_response()
{
    if (loads_committed_port->can_take())
        deallocate_lob_entries(loads_committed_port->take());
    else if (deferred_lob_dealloc)
        deallocate_lob_entries(0);
}

bool LSU::can_send_l2_prefetch_request(const Uop& uop, PrefetchType type) const
{
    bool prefetch = type != PrefetchType::NONE;
    bool has_bandwidth = (prefetch || l2_prefetches_all_to_l2_prefetch_queue) ? l2_prefetch_request->can_put()
                                                                              : l2_load_miss_request->can_put();
    // FIXME: The bandwidth of the l2_load_miss_request port is currently
    // utilized to simulate the max number of ofbs we can allocate per cycle
    // but this type of request does not need an ofb but is still
    // limited by that same bandwidth
    // setting.
    bool should_prefetch =
        (prefetch ? (l2_prefetches_for_ofb_full_prefetch || type == PrefetchType::VA_L2PF)
                  : (l2_prefetches_for_ofb_full_demand && !uop->is_atomic() && !uop->is_exclusive()));

    return should_prefetch && has_bandwidth;
}

void LSU::send_l2_read_request(MemReq& mem_req)
{
    auto l1_contents = Request::L1Contents();
    for (auto addr : l1d_cache.aliased_set_addresses(mem_req->virt_addr))
        l1_contents.add(addr);

    // If there are two outstanding requests to different lines in the same L2 set, it's too hard for
    // hardware to figure out which L2 ways are in the L1, so we just give up tell the L2 not to clear
    // any L1-present bits for this request.
    auto phys_addr = mem_req->phys_addr;
    for (const auto& ofb_handle : ofb) {
        auto ofb_addr = ofb_handle.first;
        if (!phys_addr.same_cache_line(ofb_addr) && l2_indexer->index(ofb_addr) == l2_indexer->index(phys_addr)) {
            ev_no_clear_l1_bits(mem_req);
            l1_contents.no_clear_l1_bits = true;
        }
    }

    l2_load_miss_request->put(Request::Fill(mem_req, l1_contents));
}

void LSU::send_l2_prefetch_request(MemReq& mem_req, bool prefetch)
{
    if (prefetch || l2_prefetches_all_to_l2_prefetch_queue)
        l2_prefetch_request->put(mem_req);
    else
        send_l2_read_request(mem_req);

    l2_prefetch_event(mem_req);
}

template<class Entry>
LSU::SleepReason LSU::ofb_full_recycle(Entry& entry, bool force)
{
    if (ofb.check_if_should_sleep(entry.uop, std::is_same_v<Entry, LOBEntry> ? non_spec_uop_id : 0, force))
        return {WakeCode::OFB_FULL, RecycleReason::OFB_FULL};

    if (ofb.reserved())
        return {std::nullopt, RecycleReason::OFB_ALMOST_FULL};

    return {std::nullopt, RecycleReason::L2_MISS_BANDWIDTH};
}

Uop LSU::uop_for_ofb(const OFBEntry& entry) const
{
    if (entry.opportunistic)
        return entry.trigger_uop;

    if (entry.buffer_id) {
        if (auto lob_iter = lob.find(*entry.buffer_id); lob_iter != lob.end())
            return lob_iter->second.uop;
        if (auto sob_iter = sob.find(*entry.buffer_id); sob_iter != sob.end())
            return sob_iter->second.uop;
    }

    return nullptr;
}

LSU::SLOBEntry* LSU::try_release_ofb_from_entry(uint64_t alloc_id, bool spec_lookup)
{
    if (auto lob_it = lob.find(alloc_id); lob_it != lob.end()) {
        auto& entry = lob_it->second;
        if (!entry.got_l1_data && !entry.forwarded_from_store)
            entry.go_to_sleep(WakeCode::L1_MISS);
        return &entry;
    } else if (auto sob_it = sob.find(alloc_id); sob_it != sob.end() && !spec_lookup) {
        // If OFB entry is originally allocated by an tagged
        // or non-speculative StaLd, the corresponding sob entry
        // needs to go to sleep
        auto& entry = sob_it->second;
        entry.go_to_sleep(WakeCode::L1_MISS);
        return &entry;
    }

    return nullptr;
}

template<class Entry>
LSU::SleepReason LSU::l1d_cache_miss(Entry& e, bool spec_lookup)
{
    bool unnecessary_untagged_sta_load = false;
    if constexpr (std::is_same_v<Entry, SOBEntry>) {
        AMPERE_ASSERT(speculative_sta_loads_enabled() || e.uop->is_sw_prefetch(),
                      "STAs should only cause L1D misses if store address loads for memory tagging enabled");

        unnecessary_untagged_sta_load =
            !e.tagged() && !speculative_atomic_load_allowed(e) && prefetch_sta == STAPrefetchPolicy::NEVER;
    }

    pmu_event(e.uop, pmu_num_data(archx::PMU::L1D_CACHE_MISS));
    auto phys_addr = *e.uop->phys_addr;
    auto phys_line_addr = phys_addr.line_address();
    bool needs_data = !(spec_lookup || unnecessary_untagged_sta_load);

    // Determine if there is an existing L2 request to the same physical line in OFB, if so:
    //   - if ofb_update enabled determine if existing L2 requester is younger, if so:
    //     - update OFB record to current, older request. Current req will complete from existing request's L2 response
    //     - set prior request to wake S/LOB entry and reschedule on L2 response
    //   - if not, sleep current request to wake S/LOB entry on L2 response from prior request
    if (auto* ofbe = ofb.find(phys_line_addr); ofbe) {
        auto& ofb_entry = *ofbe;

        // If the forwarding logic is going to complete this load, don't do anything if it hits an existing OFB.
        // For an untagged staLd, don't do anything if it hits an existing OFB.
        if (!needs_data)
            return {};

        // Exclusives have to make their own L2 request. Since the L2 can't handle two OFBs to the
        // same line, if there is already a request to this line we have to wait for it to complete.
        if (e.uop->is_exclusive()) {
            ev_exclusive_duplicate_ofb(e.uop);
            return {WakeCode::L1_MISS};
        }

        // If we hit an OFB that was allocated by a prefetch or a younger uop, mark it as completing
        // this demand load.
        if (ofb_entry.can_upgrade(e.uop)) {
            if (enable_ofb_update_to_older_req) {
                // If this OFB is already completing a younger uop, find it and make it wait for a fill
                // since this (older) access will be taking over the OFB -- unless it is already on its
                // way to completion
                if (ofb_entry.buffer_id && e.uop->is_older(*ofb_entry.buffer_id)) {
                    // Determine if current OFB requester is in LOB or SOB
                    if (auto* entry = try_release_ofb_from_entry(*ofb_entry.buffer_id, ofb_entry.spec_lookup); entry)
                        slob_ofb_update_give_event(entry->uop);
                }

                slob_ofb_update_take_event(e.uop);
                ofb_entry.spec_lookup = false;
                ofb_entry.buffer_id = e.uop->alloc_id;
                ofb_entry.pipe = e.uop->sch.lsu_scheduler_pipe();
            }

            // Demand request hits HW-prefetch-allocated OFB -> count as prefetch-hit for accuracy measurement purposes.
            // Double-counting is prevented by "opportunistic" and "prefetch_type" fields of OFB entry.
            // Instead of resetting prefetch bit to NONE, we "reset" it to UPGRADE types so that Gretch's
            // fill logic knows it has to capture PCP-upgrade fill but ignore Gretch-upgrade fill.
            if (ofb_entry.opportunistic) {
                if constexpr (std::is_same_v<Entry, LOBEntry>) {
                    BackendMemReq req = {ofb_entry.virt_addr, ofb_entry.phys_addr, e.uop};
                    prefetcher->execute(e.uop->alloc_id, req, false, true, true, ofb_entry.prefetch_type);
                }

                release_credit(ofb_entry.opportunistic);
            }
        } else {
            // Hit a non-prefetch OFB from an older uop -- just go to sleep to wait for the fill
            slob_sleep_on_existing_req_event(e.uop);
        }

        ofb_entry.hit(e.uop);

        // Don't fill speculative atomic loads unless specified
        if constexpr (std::is_same_v<Entry, SOBEntry>) {
            if (atomic_load_speculative_fill || !speculative_atomic_load_allowed(e))
                ofb_entry.try_to_fill();
        }

        // STALL_BACKEND_* pmus - Record the L2 hit/miss status (to collect stall cycles in the correct PMU)
        e.l2_miss = ofb_entry.l2_miss;

        return {WakeCode::L1_MISS};
    }

    if (drop_untagged_staLd_dl1_miss && unnecessary_untagged_sta_load)
        return {}; // if the page is untagged, we have the option of dropping StaLd upon a DL1 miss

    // First request to L2 for this cache line

    // If possible to send L2 request and OFB entry available, allocate OFB entry, send mem_req, and sleep S/LOB entry
    // If not, put sta/load to sleep waiting for fill buffer entry to free after a L2 response
    bool can_allocate_ofb_entry = ofb.can_allocate(e.uop);
    if (l2_load_miss_request->can_put()
        && (can_allocate_ofb_entry || (can_send_l2_prefetch_request(e.uop, PrefetchType::NONE) && !e.l2_prefetched)))
    {
        MemOp::Requester origin = can_allocate_ofb_entry ? MemOp::Requester::LSU : MemOp::Requester::LSU_PREFETCH;
        auto mem_req = make_child<MemReq>(e.uop,
                                          MemReqObj::load(e.uop->ainst()->inum().id(),
                                                          e.uop->pc(),
                                                          phys_addr,
                                                          *e.uop->virt_addr,
                                                          8,
                                                          0,
                                                          e.page_size,
                                                          enable_nontemporals && e.uop->is_nontemporal(),
                                                          origin));

        if (!can_allocate_ofb_entry) {
            if (needs_data) {
                e.l2_prefetched = true;
                send_l2_prefetch_request(mem_req, false);
                return ofb_full_recycle(e, true);
            }
            return {};
        }

        send_l2_read_request(mem_req);
        if constexpr (std::is_same_v<Entry, LOBEntry>) {
            measure_load_miss_disorder(e.uop);
        }

        auto prefetch_type = PrefetchType::NONE;
        if constexpr (std::is_same_v<Entry, SOBEntry>) {
            prefetch_type = PrefetchType::STA;
        }

        std::optional<UnableToFillReason> unable_to_fill;
        if (e.uop->is_atomic_load() && atomic_load_speculative && !atomic_load_speculative_fill)
            unable_to_fill = UnableToFillReason::ATOMIC_LOAD;
        else if (e.uop->is_sw_prefetch() && e.uop->sw_prefetch_level() != SWPrefetchLevel::L1)
            unable_to_fill = UnableToFillReason::PREFETCH_LEVEL;

        ofb.allocate(mem_req, e.uop, needs_data && !e.uop->is_sw_prefetch(), unable_to_fill, prefetch_type);

        pmu_event(e.uop, pmu_num_data(archx::PMU::L1D_CACHE_LMISS_RD));

        if constexpr (std::is_same_v<Entry, LOBEntry>) {
            prefetcher->execute(e.uop->alloc_id, {e.uop}, false, false, true, PrefetchType::NONE);
        }

        // Sleep S/LOB entry for this UOP, waiting on data return from L2/OFB
        if (needs_data)
            return {WakeCode::L1_MISS};
    } else { // Fill buffer full
        pmu_event(e.uop, pmu_num_data(archx::PMU::OFB_FULL));
        if (needs_data)
            return ofb_full_recycle(e);
    }

    return {};
}

bool LSU::l1d_cache_write_miss(const StoreRetirementOps& req, const StoreRetirementOps::Part& part, SOBEntry& e)
{
    auto phys_addr = part.phys_addr(e.uop).line_address();
    if (auto* ofbe = ofb.find(phys_addr); ofbe) {
        auto& ofb_entry = *ofbe;
        ofb_entry.demand_hit();

        // STALL_BACKEND_* pmus - Record the L2 hit/miss status (to collect stall cycles in the correct PMU)
        e.l2_miss = ofb_entry.l2_miss;
        return false;
    }

    bool can_allocate_ofb_entry = ofb.can_allocate(e.uop);
    bool have_ofb_bandwidth = l2_load_miss_request->can_put();
    if (have_ofb_bandwidth && can_allocate_ofb_entry) {
        auto mem_req = make_child<MemReq>(e.uop,
                                          MemReqObj::load(e.uop->ainst()->inum().id(),
                                                          e.uop->pc(),
                                                          part.phys_addr(e.uop),
                                                          part.virt_addr(e.uop),
                                                          req.datasize,
                                                          req.bytemask,
                                                          req.page_size,
                                                          enable_nontemporals && e.uop->is_nontemporal()));
        mem_req->type = uarch_common::MemOp::READUNIQUE;
        send_l2_read_request(mem_req);
        ofb.allocate(mem_req, e.uop);
        return true;
    }

    return false;
}

void LSU::prefetch_execute(L1PrefetchEntry next_pf, PrefetchType type)
{
    // In RTL, L1D and TLB lookup occurs concurrently, so these PMU events
    // should count on either a TLB miss or hit
    pmu_event(next_pf.parent_txn, pmu_num_data(archx::PMU::L1_PREFETCH_LD_GEN));
    // Rename PMU (Trinity)
    pmu_event(next_pf.parent_txn, pmu_num_data(archx::PMU::L1D_CACHE_HWPRF));
    if (type == PrefetchType::GRETCH)
        pmu_event(next_pf.parent_txn, pmu_num_data(archx::PMU::L1D_CACHE_HWPRF_GR));

    // Check if prefetch address is non-canonical
    auto translation = TranslationContext(
        next_pf.virt_addr,
        Bitfield(next_pf.trigger_uop ? next_pf.trigger_uop->ainst()->state().read(TCR_EL1) : 0x400c800cULL));

    if (!translation.canonical()) {
        prefetcher->drop({next_pf, type}, PrefetchDropReason::PAGE_FAULT);
        return;
    }

    // lookup L1TLB to determine if the translation is still in there
    auto tag = L1TLBTag<VirtAddr>(next_pf.virt_addr, driver_port->asid());
    auto tlb_result = dl1tlb.lookup(next_pf.parent_txn, tag, true);

    // On a TLB miss, we need to run the TLB pipeline and fill a new translation.
    if (!tlb_result) {
        bool can_prefetch_page =
            prefetcher->can_prefetch_page({next_pf, type}) && can_send_dmmu_request(next_pf.pipe_mask());
        bool can_allocate_pob = prefetcher->can_allocate_pob({next_pf, type}, curr_el);
        bool allocated_in_pob = false;

        // send translation request
        if (can_prefetch_page) {
            auto mmu_req_id = next_pf.mmu_req_id(prefetch_id);
            auto mmu_req = make_child<MMUReq>(next_pf.parent_txn, mmu_req_id, next_pf.virt_addr, false);
            send_dmmu_request(mmu_req, next_pf.pipe);

            // Allocate in POB if 1) TLB recycling feature is turned on; 2) there is space; and 3) this prefetch
            // candidate should be recycled Note: if page_fetch is off, there is no point in recycling. PCP issues
            // prefetches in non-EL0 but should not insert anything in POB (POB is frozen during non-EL0).
            if (can_allocate_pob)
                allocated_in_pob = pob.try_allocate(next_pf, type, mmu_req_id, POBState::WAIT_OTB_ACK);
        }

        if (!allocated_in_pob) {
            auto reason = can_prefetch_page && can_allocate_pob ? PrefetchDropReason::PAGE_MISS_NO_RESOURCES
                                                                : PrefetchDropReason::PAGE_MISS;
            prefetcher->drop({next_pf, type}, reason);
        }

        return;
    }

    auto page_base = tlb_result->phys_addr.page_address(tlb_result->page_size).value();
    auto untranslated_bits = next_pf.virt_addr.extract_range(tlb_result->page_size.page_bits() - 1, 0);
    // set the PA
    next_pf.phys_addr = PhysAddr(page_base | untranslated_bits);
    auto phys_line = next_pf.phys_addr->line_address();

    // Now look up the L1. If we hit, bail out.
    if (l1d_cache.lookup(
            next_pf.parent_txn, {next_pf.virt_addr, *next_pf.phys_addr}, true, false, true, phys_line.value()))
    {
        prefetcher->drop({next_pf, type}, PrefetchDropReason::CACHE_HIT);
        return;
    }

    // Determine if there is an outstanding L2 request to the new line
    if (auto* ofb_entry = ofb.find(phys_line); ofb_entry) {
        // We have hit an existing request, bail out
        auto reason = ofb_entry->opportunistic ? PrefetchDropReason::CACHE_MISS_PREFETCH_IN_FLIGHT
                                               : PrefetchDropReason::CACHE_MISS_IN_FLIGHT;

        prefetcher->drop({next_pf, type}, reason);
        return;
    }

    // See if L2 can accept the request
    bool allowed_to_allocate_ofb = prefetcher->can_allocate_ofb({next_pf, type});
    bool can_allocate_ofb_entry =
        allowed_to_allocate_ofb && ofb.can_allocate(next_pf) && l2_load_miss_request->can_put();

    bool send_as_l2_prefetch = can_send_l2_prefetch_request(next_pf.trigger_uop, type);
    if (!can_allocate_ofb_entry && !send_as_l2_prefetch) {
        bool allocated_in_pob{false};
        if (allowed_to_allocate_ofb && allocate_pob_on_ofb_full
            && prefetcher->can_allocate_pob({next_pf, type}, curr_el))
            allocated_in_pob = pob.try_allocate(next_pf, type, 0, POBState::WAIT_OFB_FULL);
        if (!allocated_in_pob)
            prefetcher->drop({next_pf, type}, PrefetchDropReason::CACHE_MISS_NO_RESOURCES);
        return;
    }

    // Determine if there is room on the pfb for this prefetch request
    if (can_allocate_ofb_entry && !send_as_l2_prefetch && !pfb.can_acquire()) {
        ev_pfb_full_event(next_pf.parent_txn);
        prefetcher->drop({next_pf, type}, PrefetchDropReason::CACHE_MISS_NO_PREFETCH_RESOURCES);
        return;
    }

    // Otherwise, we can launch this prefetch request.
    bool allocate_ofb = can_allocate_ofb_entry && pfb.can_acquire();
    MemOp::Requester origin;
    if (allocate_ofb)
        origin = MemOp::Requester::LSU;
    else if (type == PrefetchType::VA_L2PF)
        origin = MemOp::Requester::PREFETCH;
    else
        origin = MemOp::Requester::LSU_PREFETCH;

    auto mem_req = make_child<MemReq>(next_pf.parent_txn,
                                      MemReqObj::load_prefetch(next_pf.phys_addr->value(),
                                                               next_pf.consumer_pc,
                                                               *next_pf.phys_addr,
                                                               next_pf.virt_addr,
                                                               8,
                                                               0,
                                                               tlb_result->page_size.value(),
                                                               l1_prefetch_is_nontemporal,
                                                               origin,
                                                               next_pf.l2_prefetch_type));

    // A PCP prefetch which makes an L2 request trains the L1BOP. Without this, if the PCP has high
    // coverage then the L1BOP never trains.
    if (next_pf.trigger_uop && lob.contains(next_pf.trigger_uop->get_alloc_id()))
        prefetcher->execute(next_pf.trigger_uop->get_alloc_id(), {next_pf, type}, false, true, false, type);

    if (allocate_ofb) {
        send_l2_read_request(mem_req);
        if (auto [allocated, ofb_entry] = ofb.allocate(mem_req, next_pf, type); allocated) {
            ofb_entry.opportunistic = pfb.acquire();
            ev_pfb_allocate_event(mem_req);
        }
        pmu_event(next_pf.parent_txn, pmu_num_data(archx::PMU::L1_PREFETCH_L2_REQ));
    } else {
        send_l2_prefetch_request(mem_req, true);
    }

    // Only consider a request that will be later filled as a "launch", unless the prefetcher does
    // not normally allocate OFBs
    if (!allowed_to_allocate_ofb || allocate_ofb)
        prefetcher->launch({next_pf, type});
}

void LSU::l2c_snoop_request()
{
    while (l2_snoop_request->can_take()) {
        auto snoop_req = l2_snoop_request->take();
        ev_snoop(snoop_req, snoop_req->phys_addr);

        AMPERE_ASSERT(snoop_req->snoop_type != MemOp::SnoopType::SNOOP_NONE, "LSU recieved a snoop without a type");

        // Remember the address of this snoop so we can block fills in the next few cycles to avoid breaking inclusion
        snooped_lines.emplace_back(snoop_req->phys_addr, get_cycle());

        // Snoop D-cache if the evicted line had its in-L1 bit set, or if this snoop is to remove synonyms
        // For L2 evictions of lines not in the DL1 (SnoopType::OTHER), we already know the line is not there
        if (snoop_req->snoop_type == MemOp::SnoopType::SNOOP_IN_L1
            || snoop_req->snoop_type == MemOp::SnoopType::SNOOP_SYNONYM)
        {
            bool evicted = false;
            auto matches = l1d_cache.matches(snoop_req->phys_addr.line_address());
            for (const auto& set_addr : matches) {
                auto line = l1d_cache.invalidate(snoop_req, {set_addr, snoop_req->phys_addr.line_address()});
                evicted |= static_cast<bool>(line);

                if (line && line->modified) {
                    send_l2_evict_request(snoop_req, *line);
                    l1d_cache_conflicts.try_eviction(set_addr);
                    if (auto port = l1d_cache_conflicts.evictions_take_port(); port && load_pipes_used < load_pipes) {
                        auto port_mask = 1ULL << *port;
                        used_dl1_ports |= port_mask;
                        loads_sched_pipe |= port_mask;
                        load_pipes_used++;
                    }
                    ev_snoop_inval_modified(snoop_req);
                }
                ev_snoop_inval(snoop_req);
            }
            if (snoop_req->snoop_type == MemOp::SnoopType::SNOOP_IN_L1 && !evicted)
                ev_snoop_not_present(snoop_req);
        }

        // Snoop LOB on all L2 evictions, but not on synonym snoops (which are not actually evicting anything from L2)
        //
        // If a completed load is hit by a snoop, it gets RAW scrubbed, but only if there are older incomplete loads
        // in the LOB. In RTL the snoop sets a bit in the LOB, and every load execution checks for younger snoop-hit
        // loads, so the timing of this flush is a little off. But it's really rare so it doesn't matter.
        //
        // LOB snoop only matches on PA[27:6], so false hits are possible. And if the load is a split, we only check
        // PA[11:6] of the high half.
        //
        if (snoop_req->snoop_type == MemOp::SnoopType::SNOOP_IN_L1
            || snoop_req->snoop_type == MemOp::SnoopType::SNOOP_OTHER)
        {
            bool saw_incomplete_load{false};

            for (auto& lob_handle : lob) {
                auto& lob_entry = lob_handle.second;

                if (saw_incomplete_load && lob_entry.uop && lob_entry.uop->phys_addr
                    && !lob_entry.uop->is_sw_prefetch())
                {
                    bool addr_match =
                        lob_entry.uop->phys_addr->extract_range(27, 6) == snoop_req->phys_addr.extract_range(27, 6);
                    bool hi_half_match =
                        lob_entry.split
                        && ((lob_entry.uop->phys_addr->extract_range(snoop_match_high_half_upper_bit, 6) + 1)
                            == snoop_req->phys_addr.extract_range(snoop_match_high_half_upper_bit, 6));

                    if (addr_match || hi_half_match) {
                        ev_snoop_flush(snoop_req);
                        if (!lob_entry.uop->phys_addr->same_cache_line(snoop_req->phys_addr)) {
                            if (addr_match)
                                ev_snoop_flush_false_hit(snoop_req);
                            else
                                ev_snoop_flush_false_hi_hit(snoop_req);
                        }

                        lob_entry.uop->raw_scrubbed = true;
                        raw_scrub_port->put(lob_entry.uop);
                        break;
                    }
                }

                if (!lob_entry.complete)
                    saw_incomplete_load = true;
            }
        }
    }
}

bool LSU::refill_requires_eviction(VirtAddr virt_addr, PhysAddr phys_addr) const
{
    return l1d_cache_conflicts.evictions_cause_conflicts()
           && !l1d_cache.would_fill_without_victim({virt_addr, phys_addr})
           && l1d_cache.next_victim({virt_addr, phys_addr})->modified;
}

void LSU::l2c_refill_response()
{
    // Receive L2DC response at LF03
    L2RefillArbitration load_refills;
    L2RefillArbitration store_refills;
    L2RefillArbitration evict_refills;
    L2RefillArbitration all_refills;
    while (l2_load_miss_response->can_take()) {
        auto resp = l2_load_miss_response->take();
        // The L2 response comes with a flag which tells us whether the read hit
        // in L2 (so this is a real fill) or missed (a speculative fill which
        // will get canceled). These can consume a load pipeline but don't
        // participate in tag conflicts.
        if (!fill_speculative_wake && !resp->value)
            continue;

        // read hit in L2 so this fill is actually valid
        VirtAddr virt_addr;
        L1DFill fill;
        bool should_fill = true;
        if (!resp->atomic()) {
            auto& ofb_entry = ofb.at(resp->phys_addr.line_address());
            auto uop = uop_for_ofb(ofb_entry);
            virt_addr = ofb_entry.virt_addr;
            should_fill = ofb_entry.should_fill();

            if (ofb_entry.buffer_id && uop && uop->is_store()) {
                bool speculative_atomic_load = false;
                if (auto sob_iter = sob.find(*ofb_entry.buffer_id); sob_iter != sob.end())
                    speculative_atomic_load = speculative_atomic_load_allowed(sob_iter->second);

                bool needs_writeback = (speculative_atomic_load && uop->is_atomic_load()) || ofb_entry.completes_uop();
                fill = L1DFill::from_store(ofb_entry, uop, needs_writeback, resp->value);

                if (speculative_atomic_load || !store_pipes_have_tag_ports)
                    load_refills.add_execution_only(fill);
                store_refills.add_execution_only(fill);
            } else {
                fill = L1DFill::from_load(ofb_entry, uop, prefetch_fills_execute, resp->value);
                if (!ofb_entry.completes_uop() && prefetch_uses_store_pipes)
                    store_refills.add_execution_only(fill);
                else
                    load_refills.add_execution_only(fill);
            }
        } else {
            AMPERE_ASSERT(!sob.empty() && sob.begin()->second.exclusive_complete, "Atomic load fill not in SOB");
            auto& sob_entry = sob.begin()->second;
            fill = L1DFill::from_atomic_load(sob_entry.uop, sob_entry.pipe_mask(), resp->value);
            load_refills.add_execution_only(fill);
            store_refills.add_execution_only(fill);
            virt_addr = *sob_entry.uop->virt_addr;
        }

        all_refills.add_pipe(fill);

        if (resp->value) {
            if (should_fill && refill_requires_eviction(virt_addr, resp->phys_addr.line_address()))
                evict_refills.add_cache_port(fill, l1d_cache_conflicts.evictions_take_port());

            l2c_refill_inflight_addresses.push_back(virt_addr); // Checked for st-ret stall
            l2c_refill_complete_pipe.put(resp);
        }
    }

    // Indicates refill occurrence to next cycle to mark loads scheduled for recycle
    //   - This is needed to model PA tag write (refill) & read conflict (load(s) scheduled cycle after refill resp)
    if (all_refills && l1d_cache_conflicts.refills_participate_in_conflicts(all_refills))
        l2c_refill_tag_conflict_out->put(std::move(all_refills));

    // Evictions blocking load ports
    if (evict_refills && l1d_cache_conflicts.evictions_cause_conflicts())
        l2c_refill_evict_arbitration.put(std::move(evict_refills));

    // Block load ports for uop completed by fill
    if (load_refills)
        l2c_refill_load_arbitration.put(std::move(load_refills));

    // Block store ports for uop "completed" by fill
    if (store_refills)
        l2c_refill_store_arbitration.put(std::move(store_refills));
}

bool LSU::l2c_refill_pipe_consumed(const ZeroDelayOutIn<L2RefillArbitration>& l2c_refill_arbitration, uint8_t pipe_mask)
{
    return l2c_refill_arbitration.can_take() ? l2c_refill_arbitration.peek().using_pipe(pipe_mask) : false;
}

uint8_t LSU::l2c_refill_pipes(const ZeroDelayOutIn<L2RefillArbitration>& l2c_refill_arbitration)
{
    return l2c_refill_arbitration.can_take() ? l2c_refill_arbitration.peek().pipes : 0;
}

void LSU::l2c_refill_tag_conflicts()
{
    if (l1d_cache_conflicts.refill_conflicts() && l2c_refill_tag_conflict_in->can_read()) {
        auto refills = l2c_refill_tag_conflict_in->read();
        l1d_cache_conflicts.assign_refill_tag_conflicts(refills);

        // On older projects where enable_fill_read_chunk_bank_conflicts == false, fills block both pipes. On
        // newer projects, they block only pipe 1.
        // FIXME: This assumes that if there are more than two pipes, all but the first one get blocked by
        // a fill (can_load_schedule() does the same). That's probably not right.
        if (fill_conflict_blocks_schedule) {
            auto& uop = std::get<Uop>(refills.uops.front());
            for (unsigned pipe = 0; pipe < IXU_PARTITIONS; ++pipe) {
                if (l1d_cache_conflicts.refill_conflicts_on_pipe(pipe))
                    load_pipe_occupancy_event(uop, pipe_partition(1U << pipe), pipe_occupant(PipeOccupant::FILL_BLOCK));
            }
        }
    } else {
        l1d_cache_conflicts.assign_refill_tag_conflicts();
    }
}

void LSU::l2c_refill_evict_conflicts()
{
    if (l1d_cache_conflicts.evictions_cause_conflicts() && l2c_refill_evict_arbitration.can_take()) {
        const auto& fill = l2c_refill_evict_arbitration.peek();
        for (const auto& e : fill.addresses) {
            auto virt_addr = e.addr.line_address();
            if (!l1d_cache_conflicts.try_eviction(virt_addr) && drop_fill_no_eviction_port) {
                auto* ofb_entry = ofb.find(virt_addr);
                if (!ofb_entry)
                    continue;

                bool drop_uop = false;
                if (!incomplete_fill_can_complete && ofb_entry->buffer_id) {
                    if (auto* entry = try_release_ofb_from_entry(*ofb_entry->buffer_id, ofb_entry->spec_lookup); entry)

                        ev_refill_completion_dropped(entry->uop);
                    drop_uop = true;
                }
                if (drop_uop)
                    ofb_entry->drop_uop(UnableToFillReason::EVICTION_INCOMPLETE);
                else
                    ofb_entry->drop(UnableToFillReason::EVICTION_INCOMPLETE);
            }
        }

        used_dl1_ports |= fill.cache_ports;
    }
}

void LSU::l2c_refill_evict_schedule()
{
    if (l1d_cache_conflicts.evictions_cause_conflicts() && l2c_refill_evict_arbitration.can_take()) {
        auto fill = l2c_refill_evict_arbitration.take();
        for (const auto& [valid, completes, uop] : fill.uops) {
            if (valid && fill.cache_ports)
                load_pipe_occupancy_event(uop, pipe_partition(fill.cache_ports), pipe_occupant(PipeOccupant::EVICT));
        }
    }

    while (!evictions.empty() && can_evict_to_l2()) {
        if (evict_modified_via_l2_pwc)
            if (auto sob_iter = next_store_to_retire();
                sob_iter != sob.end() && is_wide_atomic_load(sob_iter->second.uop))
                break;

        evict(evictions.front());
        evictions.pop_front();
    }
}

void LSU::l2c_refill_schedule(ZeroDelayOutIn<L2RefillArbitration>& l2c_refill_arbitration)
{
    if (!l2c_refill_arbitration.can_take())
        return;

    bool is_store = &l2c_refill_arbitration == &l2c_refill_store_arbitration;
    auto fill = l2c_refill_arbitration.take();
    for (const auto& [valid, completes, uop] : fill.uops) {
        if (completes && uop) {
            if (uop->is_store()) {
                if (auto sob_it = sob.find(uop->alloc_id); sob_it == sob.end() || !sob_it->second.retiring()) {
                    if (is_store)
                        uop_sta_execute_event(uop);
                    else
                        uop_sta_load_execute_event(uop);
                }
            } else {
                uop_ld_execute_event(uop);
            }
        }

        if (valid && fill.pipes) {
            (is_store ? store_pipe_occupancy_event : load_pipe_occupancy_event)(
                uop, pipe_partition(fill.pipes), pipe_occupant(PipeOccupant::REFILL));
        } else if (fill_speculative_wake_ixu && enable_load_cancel && uop && completes && !is_store
                   && (uop->is_atomic_load() ? sob.find(uop->alloc_id) != sob.end()
                                             : lob.find(uop->alloc_id) != lob.end()))
        {
            load_cancel_out->put(uop);
        }
    }
}

void LSU::l2c_refill_complete()
{
    while (l2c_refill_complete_pipe.can_take()) {
        auto mem_resp = l2c_refill_complete_pipe.take();
        auto cacheline = mem_resp->phys_addr.line_address();
        OFBEntry* ofb_ptr = nullptr;

        if (!mem_resp->atomic()) {
            ofb_ptr = ofb.find(cacheline);
            AMPERE_ASSERT(ofb_ptr, "LSU got fill for PA {} but there was no OFB", cacheline);
            auto& ofb_entry = *ofb_ptr;

            // treat this as a prefetch fill (which does not feed a load) if:
            // - it was a prefetch, OR
            // - it was a speculative lookup from a load which was completed by store forwarding, OR
            // - it was an untagged staLd
            // - the uop was flushed, but it has a valid physical address (the load was in the shadow of a raw scrub)
            std::optional<uint64_t> req_buffer_id;
            auto req_uop = ofb_entry.trigger_uop;
            auto req_prefetch_type = ofb_entry.prefetch_type;
            bool is_prefetch =
                ofb_entry.speculative() || (!ofb_entry.buffer_id && !ofb_entry.pwc_id && cacheline != 0x0_pa);
            auto req_virt_addr = ofb_entry.virt_addr;
            auto req_phys_addr = ofb_entry.phys_addr;
            if (is_prefetch) {
                if (!ofb_entry.is_prefetch())
                    req_prefetch_type = PrefetchType::NONE;
                if (req_uop)
                    req_buffer_id = req_uop->alloc_id;
                l2c_refill_prefetch_complete(
                    mem_resp, ofb_entry, (bool)ofb_entry.opportunistic /* hardware prefetch */);
            } else { // demand
                AMPERE_ASSERT(ofb_entry.is_demand(), "Demand OFB entry has the incorrect prefetch type");
                l2c_refill_demand_complete(mem_resp, ofb_entry);

                req_uop = nullptr;
                if (ofb_entry.buffer_id) {
                    if (auto lob_iter = lob.find(*ofb_entry.buffer_id); lob_iter != lob.end()) {
                        req_uop = lob_iter->second.uop;
                        req_buffer_id = ofb_entry.buffer_id;
                        auto [split, virt_addr] = lob_iter->second.virt_addr_of(ofb_entry.virt_addr);
                        req_virt_addr = virt_addr;
                        req_phys_addr = split ? ofb_entry.phys_addr : *lob_iter->second.phys_addr();
                    }
                }
            }

            BackendMemReq req = {req_virt_addr, req_phys_addr, req_uop, req_prefetch_type};
            prefetcher->fill(req_buffer_id.value_or(0), req, is_prefetch, ofb_entry.was_prefetch());
        } else {
            auto& sob_entry = sob.begin()->second;
            atomic_load_receives_data(sob_entry);

            if (is_start_of_wide_atomic_load(sob_entry.uop)) {
                for (auto next_sob_iter = std::next(sob.begin());
                     next_sob_iter != sob.end()
                     && is_next_part_of_wide_atomic_load(next_sob_iter->second.uop, sob_entry.uop);
                     ++next_sob_iter)
                {
                    if (next_sob_iter->second.is_following_part_of_wide_atomic_load(sob_entry.uop)) {
                        auto& next_sob_entry = next_sob_iter->second;
                        next_sob_entry.exclusive_receives_data();
                    }
                }
            }
        }

        if (ofb_ptr) {
            if (ofb_ptr->pwc_id) {
                pwc_response->put(LSUPWC::Response{
                    *ofb_ptr->pwc_id,
                    mem_resp->unique ? LSUPWC::ResponseAction::FILLED : LSUPWC::ResponseAction::FILLED_NOT_UNIQUE});
            }

            ofb.deallocate(cacheline, mem_resp);
        }

        l2c_refill_inflight_addresses.pop_back();
        l2c_refill_wakeup_pipe.put(mem_resp);
    }
}

void LSU::l2c_refill_wakeup()
{
    while (l2c_refill_wakeup_pipe.can_take()) {
        auto mem_resp = l2c_refill_wakeup_pipe.take();
        // Wakeup S/LOB entries slept on the same PhysAddr cache line request, or OFB full
        wakeup_l2c_dependents(mem_resp->phys_addr);
    }
}

void LSU::l2c_refill_prefetch_complete(MemResp& mem_resp, OFBEntry& ofb_entry, bool hw_prefetch)
{
    if (mem_resp->value) {
        if (ofb_entry.should_fill()) {
            l1d_cache_refill(mem_resp,
                             ofb_entry.virt_addr,
                             false,
                             ofb_entry.exclusive,
                             hw_prefetch,
                             false /* not SW prefetch*/,
                             ofb_entry.is_prefetch() ? ofb_entry.prefetch_type : PrefetchType::NONE);
        } else {
            ev_refill_dropped(mem_resp, ev_refill_drop_reason(*ofb_entry.unable_to_fill));
        }
    }

    if (ofb_entry.opportunistic) {
        pmu_event(mem_resp, pmu_num_data(archx::PMU::L1_PREFETCH_LD_FILL));
        // Rename PMU (Trinity)
        pmu_event(mem_resp, pmu_num_data(archx::PMU::L1D_CACHE_REFILL_HWPRF));
        if (ofb_entry.prefetch_type == PrefetchType::GRETCH)
            pmu_event(mem_resp, pmu_num_data(archx::PMU::L1D_CACHE_REFILL_HWPRF_GR));
        release_credit(ofb_entry.opportunistic);

        ev_pfb_deallocate_event(mem_resp);
    }
}

void LSU::l2c_refill_demand_complete(MemResp& mem_resp, OFBEntry& ofb_entry)
{
    // Fill returned L2 value to L1D
    if (ofb_entry.should_fill()) {
        l1d_cache_refill(mem_resp,
                         ofb_entry.virt_addr,
                         static_cast<bool>(ofb_entry.pwc_id),
                         ofb_entry.exclusive,
                         false /* not HW prefetch */,
                         ofb_entry.sw_prefetch,
                         PrefetchType::NONE);
    } else {
        ev_refill_dropped(mem_resp, ev_refill_drop_reason(*ofb_entry.unable_to_fill));
    }

    if (!ofb_entry.buffer_id) {
        // Dropped due to flush
        if (!ofb_entry.pwc_id)
            l2_resp_drop_event(mem_resp);
        return;
    }

    // Determine if OFB requester is in LOB or SOB
    if (auto lob_it = lob.find(*ofb_entry.buffer_id); lob_it != lob.end()) {
        auto& lob_entry = lob_it->second;
        lob_entry.wakeup();

        // Immediately complete the oldest load waiting for this data, unless it is still in the pipeline
        // Also check for the LOB entry already being complete, which can happen for SW prefetch uops
        if (!lob_entry.executing && !lob_entry.complete)
            load_complete_uop(lob_entry.uop, true /*L2C refill*/);
    }
    // It is not guaranteed that OFB requester is in SOB. Example: prfm
    else if (auto sob_it = sob.find(*ofb_entry.buffer_id); sob_it != sob.end())
    {
        auto& sob_entry = sob_it->second;

        if (sob_entry.retiring()) {
            AMPERE_ASSERT(mem_resp->unique, "Fill for store not returned in unique");
        } else if (speculative_sta_loads_enabled()) {
            // Overview of what happens upon STA-load refill:
            // 1. If the uop is still in the load pipeline, drop fill-bypass
            //    (scenario: STA-load took over another OFB, making this refill faster than load pipeline recycle
            //    latency)
            // 2. If the uop is still in the store pipeline somewhere, just mark the STA-load completion fields. Do
            // not attempt to complete STA.
            //    (Scenario: STA-load took over another OFB, making this refill faster than the progress on the
            //    store pipe).
            // 3. If this is the refill for low half of a split, move the SOB entry to high half.
            // 4. If this is the completed refill, write back to inform gpc unless
            //    a) waiting for SUS Data; b) required to recycle due to failure to RAW scrub in-flight load.

            AMPERE_ASSERT(sob_entry.tagged(), "Fill of an untagged StaLd should not be treated as demand fill.\n");

            sob_entry.wakeup();
            // Verify STA-load is not in the load pipeline
            if (!sob_entry.recycle) {
                bool low_half_of_split = !sta_load_complete_uop(sob_entry, true);
                // Verify SOB entry is not in the store pipeline somewhere
                if (!sob_entry.executing) {
                    if (low_half_of_split) {
                        // Mark the low half complete and send the uop back through the pipeline
                        adjust_sta_for_split(sob_entry);

                        // no need to set must_recycle because addr_complete is not set, so it will re-execute
                        // use ld split recycle port. Using either ld recycle complete or
                        // st recycle complete port can cause write BW error on these ports
                        // because 2 things could be scheduled down load/store pipes.
                        if (store_pipes_have_tag_ports)
                            recycle(sob_entry, st_recycle_complete.out);
                        else
                            recycle(sob_entry, ld_split_recycle_complete.out);
                    } else { // either not a split or high half of split
                        if (sob_entry.split)
                            sob_entry.restore_base_addr();

                        if (speculative_atomic_load_allowed(sob_entry) && !sob_entry.recycle && !sob_entry.wrote_back)
                            writeback_sta_load(sob_entry, sob_entry.uop);

                        try_completing_sta(
                            sob_entry, true, speculative_atomic_load_allowed(sob_entry) || !store_pipes_have_tag_ports);
                    }
                }
            }
        }
    }
}

void LSU::evict(MemReq mem_req)
{
    if (evict_modified_via_l2_pwc)
        l2_store_miss_request->put(mem_req);
    else
        l2_writeback_request->put(mem_req);
}

void LSU::pwc_schedule()
{
    while (pwc_request->can_take() && pwc_response->can_put()) {
        const auto& peeked = pwc_request->peek();
        if (auto reason = check_store_retirement_stalls(peeked.req->virt_addr, peeked.write_through); reason) {
            pwc_schedule_stall_event(peeked.req, st_retire_stall_cause_data(*reason));
            break;
        }

        auto request = pwc_request->take();
        LSUPWC::Response response{
            request.id,
            request.prefetch_only ? LSUPWC::ResponseAction::PREFETCHED : LSUPWC::ResponseAction::NO_BANDWIDTH};

        l1d_cache_invalidate_aliases(request.req,
                                     request.req->virt_addr,
                                     request.req->phys_addr,
                                     !(request.req->exclusive() || request.req->atomic()));

        bool has_ofb = false;
        auto* ofb_entry = ofb.find(request.req->phys_addr.line_address());
        auto l1d_hit = l1d_cache.lookup(request.req,
                                        {request.req->virt_addr, request.req->phys_addr},
                                        !(enable_nontemporals && request.req->nontemporal) /* update LRU */,
                                        false,
                                        false,
                                        request.req->phys_addr.value());

        bool write_through = request.write_through_after_hit(static_cast<bool>(l1d_hit));

        if (l1d_hit && (write_through || l1d_hit->unique)) {
            if (!request.prefetch_only) {
                l1d_hit->modified = true;
                store_retirement_write_cache(request.req, request.req->virt_addr, request.req->size);
                response.action = LSUPWC::ResponseAction::STORED;
            }
        } else if (!write_through || (pwc->should_wait_for_ofb() && ofb_entry && !request.write_through)) {
            if (ofb_entry) {
                if (request.prefetch_only) {
                    has_ofb = true;
                } else if (!ofb_entry->pwc_id) {
                    ofb_entry->demand_hit();
                    ofb_entry->pwc_id = request.id;
                    has_ofb = true;
                }
            } else if (l2_load_miss_request->can_put() && ofb.can_allocate()) {
                auto l2_req = make_child<MemReq>(request.req, *request.req);
                l2_req->type = uarch_common::MemOp::READUNIQUE;
                send_l2_read_request(l2_req);
                auto prefetch_type = request.prefetch_only ? PrefetchType::STORE : PrefetchType::NONE;
                // Even though we allocate here we shouldn't need to in reality so we don't count
                // this allocation against the ofb allocation per partition bandwidth
                ofb.allocate(l2_req, request.id, prefetch_type);
                has_ofb = true;
            }

            response.action =
                has_ofb ? (request.prefetch_only ? LSUPWC::ResponseAction::PREFETCHED : LSUPWC::ResponseAction::MISSED)
                        : LSUPWC::ResponseAction::NO_OFB;
        }

        if (write_through && !request.prefetch_only && !has_ofb) {
            if (l2_store_miss_request->can_put()) {
                l2_store_miss_request->put(request.req);
                response.action = LSUPWC::ResponseAction::L2;
            } else {
                response.action = LSUPWC::ResponseAction::NO_BANDWIDTH;
            }
        }

        pwc_response->put(response);
    }

    bool any_deallocations = false;
    while (pwc_deallocate->can_take()) {
        auto [id, phys_addr] = pwc_deallocate->take();
        any_deallocations = true;
        if (pwc->can_wakeup_precise())
            store_wakeup_dependent_loads(id, WakeCode::PWC);
    }

    if (any_deallocations && pwc->can_wakeup() && !pwc->can_wakeup_precise())
        wakeup_by_code(WakeCode::PWC, lob);
}

template<class Entry>
void LSU::dl1_tlb_miss(Entry& e)
{
    // If STA/STA-load co-issue through store/load pipe, both may encounter DL1 TLB miss, but only one request is sent
    // out to MMU. In Panthera, STA-load for this SOB entry has already encountered DL1 TLB miss (in Panthera, all loads
    // are scheduled before all stores). The following prevents another request from being sent out to MMU.
    if (std::is_same_v<Entry, SOBEntry> && speculative_sta_loads_enabled() && e.wake_code
        && (e.wake_code == WakeCode::OTB_ACK))
        return;

    // If possible to send MMU request, send request and sleep S/LOB entry
    // If not, uop must go through pipeline again
    if (can_send_dmmu_request(e.pipe_mask())) {
        auto mmu_req =
            make_child<MMUReq>(e.uop, e.uop->alloc_id, *e.uop->virt_addr, e.uop->is_store(), is_non_speculative(e));
        send_dmmu_request(mmu_req, e.uop->sch.lsu_scheduler_pipe());

        // Sleep S/LOB entry for this UOP, waiting on data return from MMU.
        // OTB_ACK is a placeholder until we see the ack from the MMU
        // confirming whether or not it was allocated in the OTB.
        e.go_to_sleep(WakeCode::OTB_ACK);
    } else { // MMU cannot take miss request, UOP should recycle
        if constexpr (!std::is_same_v<Entry, SOBEntry>) {
            if (!store_pipes_have_tag_ports)
                AMPERE_ERROR("Only STAs should fail to make MMU requests if loads consume all slots");
        }
    }
}

template<typename Buffer>
void LSU::wakeup_by_code(WakeCode code, Buffer& slob)
{
    for (auto& [_, slob_entry] : slob)
        if (slob_entry.wake_code == code)
            slob_entry.wakeup();
}

void LSU::dmmu_refill_response()
{
    while (mmu_miss_response->can_take()) {
        auto mmu_resp = mmu_miss_response->take();

        dmmu_refill_complete_pipe.put(mmu_resp);
        dmmu_refill_wakeup_pipe.put(mmu_resp);
        block_on_dl1tlb_refill++;
    }
}

void LSU::dmmu_refill_complete()
{
    while (dmmu_refill_complete_pipe.can_take()) {
        auto mmu_resp = dmmu_refill_complete_pipe.take();

        if (!mmu_resp->faulted) {
            // Write returned physical addr translation to DL1_TLB
            auto page_size = mmu_resp->faulted ? PageSize{PageSize::Size::PS_4KB} : mmu_resp->page_size;
            auto tag = L1TLBTag<VirtAddr>(mmu_resp->virt_addr, mmu_resp->asid);
            auto entry = L1TLBEntry(mmu_resp->phys_addr, page_size);

            // Overall L1D_TLB_REFILL event is computed in rollup_formula.py as L1D_TLB_REFILL_RD + L1D_TLB_REFILL_WR
            if (mmu_resp->is_store)
                pmu_event(mmu_resp, pmu_num_data(archx::PMU::L1D_TLB_REFILL_WR));
            else
                pmu_event(mmu_resp, pmu_num_data(archx::PMU::L1D_TLB_REFILL_RD));

            dl1tlb.refill(mmu_resp, tag, entry);

        } else {
            // Sleep entries which receive a faulting request until they are
            // the oldest and can re-try
            if (mmu_resp->is_store)
                sleep_mmu_dependents(mmu_resp->id, WakeCode::RETRY_FAULT, sob);
            else
                sleep_mmu_dependents(mmu_resp->id, WakeCode::RETRY_FAULT, lob);
        }

        // Unblock load and STA pipeline from blocking on MMU response
        block_on_dl1tlb_refill--;

        // process MMU responses for POB
        if (prefetcher->can_replay())
            pob.handle_mmu_response(mmu_resp->id, mmu_resp->faulted);
    }
}

void LSU::dmmu_refill_wakeup()
{
    while (dmmu_refill_wakeup_pipe.can_take()) {
        auto mmu_resp = dmmu_refill_wakeup_pipe.take();

        if (!mmu_resp->faulted) {
            // Wakeup S/LOB entries slept on the same cache line request, or OTB full.
            if (mmu_resp->is_store)
                wakeup_mmu_dependents(mmu_resp->id, sob);
            else
                wakeup_mmu_dependents(mmu_resp->id, lob);
        }
    }
}

template<typename Buffer>
void LSU::dmmu_refill_sleep_or_wake(MMUAck mmu_ack, Buffer& slob)
{
    if (auto slob_it = slob.find(mmu_ack->id); slob_it != slob.end()) {
        auto& slob_entry = slob_it->second;
        if (slob_entry.wake_code == WakeCode::OTB_ACK) {
            if (mmu_ack->otb_id.has_value()) {
                slob_entry.go_to_sleep(WakeCode::TLB_MISS, mmu_ack->otb_id.value());
            } else { // MMU Req not acknowledged, wakeup for recycle
                // load_pipeline_execute() already put this uop into the recycle delay pipe
                if constexpr (std::is_same_v<LOB, Buffer>) {
                    uop_ld_recycle_event(slob_entry.uop, ld_recycle_reason(RecycleReason::OTB_FULL));
                } else {
                    uop_st_recycle_event(slob_entry.uop, st_recycle_reason(RecycleReason::OTB_FULL));
                }

                if (otb_full_sleep)
                    slob_entry.go_to_sleep(WakeCode::OTB_FULL);
                else
                    slob_entry.wakeup();
            }
        }
    }
}

// dl1_tlb_miss sets wake_code to OTB_ACK in the previous cycle,
// and only sets it to TLB_MISS once it receives an acknowledgement from the MMU.
void LSU::dmmu_refill_acks()
{
    while (mmu_miss_ack->can_take()) {
        auto mmu_ack = mmu_miss_ack->take();

        dmmu_refill_sleep_or_wake(mmu_ack, lob);
        dmmu_refill_sleep_or_wake(mmu_ack, sob);

        // process MMU acks for POB.
        if (prefetcher->can_replay())
            pob.handle_mmu_ack(mmu_ack->id, mmu_ack->otb_id);
    }
}

void LSU::dmmu_wakeup_otb_full()
{
    if (!mmu_can_accept->can_take())
        return;

    mmu_can_accept->take();

    wakeup_by_code(WakeCode::OTB_FULL, lob);
    wakeup_by_code(WakeCode::OTB_FULL, sob);
}

void LSU::tlb_invalidations()
{
    while (tlbi_request_in->can_take()) {
        auto tlbi = tlbi_request_in->take();
        auto& ainst = tlbi->insn->archx_instruction;

        // For TLBI by VA, invalidate specific entries. For everything else, just clear the whole ITLB.
        // FIXME: this still over-invalidates in many cases
        if (ainst->is_tlbi_by_va())
            dl1tlb.invalidate_by_va(ainst->tlbi_by_va_asid(), ainst->tlbi_by_va_ttl(), ainst->tlbi_by_va_addr());
        else
            dl1tlb.clear();
    }
}

// LOB/SOB entries with faulting translations are not allowed to re-try until
// they are strictly non-speculative. Also, loads in STLR sleep are woken when
// they are non-spec and the L2 indicates there is no store-release in progress,
// to cover some cases where there was no L2 signal to trigger the normal wake.
// This approximates this by waking up the oldest LOB/SOB entry if its dmmu refill
// request previously faulted. Note that this may end up retrying faulting translations
// early, or late if a faulting load waits for an older store to retire and exit the
// SOB (not just commit).
void LSU::wakeup_non_speculative()
{
    if (!non_spec_uop_id)
        return;

    if (auto lob_iter = lob.find(non_spec_uop_id); lob_iter != lob.end()) {
        auto& lob_entry = lob_iter->second;
        if (!lob_entry.waiting_till_non_spec()
            || (lob_entry.wake_code == WakeCode::STLR
                && (retire_pending_store_releases || (pwc && pwc->pending_store_release())
                    || l2_pending_store_release)))
            return;

        if (lob_entry.wake_code == WakeCode::STLR)
            uop_ld_stlr_nonspec_wakeup_event(lob_entry.uop);

        lob_entry.wakeup();
    } else if (auto sob_iter = sob.find(non_spec_uop_id); sob_iter != sob.end()) {
        if (auto& sob_entry = sob_iter->second; sob_entry.waiting_till_non_spec())
            sob_entry.wakeup();
    }
}

void LSU::wakeup_l2c_dependents(PhysAddr phys_addr)
{
    // Wakeup all S/LOB entries dependent on returning physical address of L2C response or sleeping on OFB full.
    // Can then be selected for execution
    for (auto& [_, lob_entry] : lob)
        if (lob_entry.should_wake_from_fill(phys_addr))
            lob_entry.wakeup();

    for (auto& [_, sob_entry] : sob)
        if (sob_entry.should_wake_from_fill(phys_addr))
            sob_entry.wakeup();

    pob.ofb_dealloc();
}

template<typename Buffer>
void LSU::wakeup_mmu_dependents(uint64_t otb_id, Buffer& slob)
{
    // Wakeup all LOB entries dependent on returning MMU response
    // Can then be selected for execution
    for (auto& [_, slob_entry] : slob) {
        if (slob_entry.is_asleep_on_pending_mmu_req(otb_id))
            slob_entry.wakeup();
    }
}

void LSU::release_mlb_entries()
{
    while (mlb_entry_release.can_take()) {
        mlb_entry_release.take();

        mlb_in_use--;
        AMPERE_ASSERT(mlb_in_use >= 0, "MLB in use went negative!");
        mlb_wakeup.put(true);
    }
}

void LSU::wakeup_mlb_dependents()
{
    bool saw_wake{false};

    // A load in BASES_LOADED gets woken up when it becomes non-spec, to avoid deadlocks
    if (!lob.empty()) {
        auto& lob_head = lob.begin()->second;
        if (lob_head.wake_code == WakeCode::BASES_LOADED) {
            lob_head.wakeup();
            uop_ld_mlb_wakeup_event(lob_head.uop);
        }
    }

    while (mlb_wakeup.can_take())
        saw_wake = mlb_wakeup.take();

    if (saw_wake) {
        // Wakeup all LOB entries waiting for an MLB entry
        // Can then be selected for execution
        for (auto& [_, lob_entry] : lob) {
            if (lob_entry.wake_code == WakeCode::BASES_LOADED) {
                lob_entry.wakeup();
                uop_ld_mlb_wakeup_event(lob_entry.uop);
            }
        }
    }
}

void LSU::wakeup_stret_dependents()
{
    while (stret_wakeup.can_take()) {
        uint64_t wake_id = stret_wakeup.take();
        store_wakeup_dependent_loads(wake_id, WakeCode::ST_RETIRE);
    }
}

template<typename Buffer>
void LSU::sleep_mmu_dependents(uint64_t otb_id, WakeCode wake_code, Buffer& slob)
{
    // Sleep all SLOB entries dependent on MMU response
    for (auto& [_, slob_entry] : slob) {
        if (slob_entry.is_asleep_on_pending_mmu_req(otb_id)) {
            slob_entry.go_to_sleep(wake_code);

            // Mark the uop as having faulted, and we will fake a DTLB hit the next time around -- otherwise we
            // enter an infinite fault loop, since nothing in panthera is actually going to correct the page tables.
            if (wake_code == WakeCode::RETRY_FAULT)
                slob_entry.uop->faulted = true;
        }
    }
}

void LSU::deallocate_lob_entries(uint loads_committed)
{
    // Deallocates based off feedback from GPC on how many loads are retired this cycle
    auto entries_to_dealloc = loads_committed + deferred_lob_dealloc;
    bool can_commit_next = true;

    for (uint dealloc_count = 0, max_lob_dealloc = entries_to_dealloc;
         can_commit_next && dealloc_count < max_lob_dealloc;
         dealloc_count++)
    {
        AMPERE_ASSERT_ERROR(!lob.empty(), "GPC indicates committed load(s), but LOB empty");

        auto& lob_entry = lob.begin()->second;
        AMPERE_ASSERT_ERROR(lob_entry.complete, "GPC indicates committed load(s), but LOB entry not yet complete");

        can_commit_next = prefetcher->commit(lob.begin()->first, {lob_entry.uop});

        auto new_el = lob_entry.uop->instructions.front()->archx_instruction->state().exception_level();
        if (new_el != curr_el)
            pob.el_change(curr_el, new_el);
        curr_el = new_el;

        preview_rob.erase(lob_entry.uop->alloc_id);
        credit_return->put(std::move(lob_entry.uop->lobid));
        lob.erase(lob.begin());
        entries_to_dealloc--;
    }

    // Remember any entries which have committed but weren't deallocated this cycle
    deferred_lob_dealloc = entries_to_dealloc;
}

void LSU::clear_invalid_entries(uint64_t flush_alloc_id)
{
    // Invalidate LOB and SOB entries younger than flushed alloc_id
    for (auto lob_handle = lob.begin(); lob_handle != lob.end();) {
        auto& lob_entry = lob_handle->second;
        if (lob_entry.uop->is_younger(flush_alloc_id)) {
            ofb.flush(lob_handle->first);

            auto flush = make<Flush>(lob_entry.uop->pc(),
                                     lob_entry.uop->pc(),
                                     lob_entry.uop->flush_instruction()->fetch,
                                     lob_entry.uop->flush_instruction(),
                                     lob_handle->first,
                                     FlushType::NOT_KNOWN);
            flush->memory_buffer_id = lob_entry.uop->lob_id_val;
            prefetcher->flush(flush);

            if (lob_entry.has_mlb)
                mlb_entry_release.put(lob_entry.uop->alloc_id);
            lob_handle = lob.erase(lob_handle);
        } else {
            lob_handle++;
        }
    }

    // Invalidate LUB entries younger than flushed alloc_id
    for (auto lub_handle = lub.begin(); lub_handle != lub.end();) {
        auto& lub_entry = lub_handle->second;
        if (lub_entry->is_younger(flush_alloc_id)) {
            lub_handle = lub.erase(lub_handle);
        } else {
            lub_handle++;
        }
    }
    AMPERE_ASSERT(lub.size() < 4096, "Too many LUB entries");

    for (auto sob_handle = sob.begin(); sob_handle != sob.end();) {
        if (is_younger(sob_handle->first, flush_alloc_id)) {
            if (speculative_sta_loads_enabled())
                ofb.flush(sob_handle->first);

            // HERE we do both
            return_sob_credit(sob_handle, true);
            sob_handle = delete_sob_entry(sob_handle, true);
        } else {
            sob_handle++;
        }
    }

    for (auto preview_rob_handle = preview_rob.begin(); preview_rob_handle != preview_rob.end();) {
        if (preview_rob_handle->second->is_younger(flush_alloc_id)) {
            preview_rob_handle = preview_rob.erase(preview_rob_handle);
        } else {
            preview_rob_handle++;
        }
    }
}

bool LSU::can_writeback_uop(const Uop& uop, bool as_load) const
{
    return writeback_mem[lsu_port_lookup(uop, as_load)]->can_put();
}

bool LSU::can_complete_uop(const Uop& uop) const
{
    return complete_mem[lsu_port_lookup(uop)]->can_put();
}

void LSU::writeback_load(LOBEntry& lob_entry)
{
    auto& uop = lob_entry.uop;
    AMPERE_ASSERT(
        !lob_entry.complete, "Attempt to complete a load twice txid_{} allocid_{}", uop->txid().value, uop->alloc_id);
    lob_entry.complete = true;

    auto lsu_port = lsu_port_lookup(uop);
    writeback_mem[lsu_port]->put(uop);
    complete_mem[lsu_port]->put({uop, false});
}

bool LSU::early_store_completion_allowed() const
{
    return sus_skip_final_sta || instantly_complete_uops_not_needing_sta || dsb_skip_final_sta
           || dmb_all_skip_final_sta;
}

bool LSU::can_complete_exclusive_store(const SOBEntry& sob_entry) const
{
    return !real_exclusives || (!sob_entry.uop->is_exclusive() && !sob_entry.uop->is_atomic_load())
           || sob_entry.exclusive_is_ready_to_complete();
}

bool LSU::can_complete_non_exclusive_store(const SOBEntry& sob_entry) const
{
    return !sob_entry.uop->is_single_uop_store()
           || ((sob_entry.fat_sus || sob_entry.data_low_complete) && (!enable_mte || sob_entry.tag_check_complete()));
}

bool LSU::can_complete_store(const SOBEntry& sob_entry) const
{
    return (!enable_mte || sob_entry.tag_check_complete()) && can_complete_exclusive_store(sob_entry)
           && can_complete_non_exclusive_store(sob_entry);
}

bool LSU::sta_only_needs_completion(const SOBEntry& sob_entry) const
{
    return sob_entry.addr_complete && sob_entry.must_recycle && !sob_entry.executing && can_complete_store(sob_entry);
}

bool LSU::sta_needs_schedule_for_completion(const SOBEntry& entry) const
{
    return entry.uop && (entry.is_ready_for_completion() || sta_only_completes(entry)) && can_complete_store(entry);
}

void LSU::writeback_store(SOBEntry& sob_entry, const Uop& uop, bool as_load)
{
    bool same_uop = sob_entry.uop == uop;
    if (!same_uop || !sob_entry.wrote_back)
        writeback_mem[lsu_port_lookup(uop, as_load)]->put(uop);

    if (uop->is_single_uop_store() || same_uop)
        sob_entry.wrote_back = true;
}

void LSU::try_completing_store(SOBEntry& sob_entry, const Uop& uop, bool as_load)
{
    if (early_store_completion_allowed() && !as_load)
        pending_store_completions.emplace(uop->store_associated_sta_id, true);
    else
        complete_store(sob_entry, uop, false, as_load);
}

void LSU::complete_store(SOBEntry& sob_entry, const Uop& uop, bool writeback, bool as_load)
{
    bool same_uop = sob_entry.uop == uop;
    if (!same_uop || !sob_entry.complete)
        complete_mem[lsu_port_lookup(uop, as_load)]->put({uop, writeback});

    if (uop->is_single_uop_store() || same_uop)
        sob_entry.complete = true;
}

void LSU::skip_sta_and_complete(SOBEntry& sob_entry, bool writeback, bool wakeup)
{
    // immediate completion, skips pipeline dispatch
    sob_entry.addr_complete = true;
    sob_entry.data_low_complete = true;
    sob_entry.must_recycle = false;
    if (!sob_entry.uop->phys_addr) {
        sob_entry.uop->phys_addr = 0x0_pa;
        sob_entry.page_type = PageType::NON_ADDR;
    } else if (sob_entry.page_type == PageType::UNKNOWN) {
        sob_entry.page_type = PageType::NON_ADDR;
    }

    AMPERE_ASSERT(!sob_entry.needs_immediate_writeback(), "Early completion of STA which actually needs writeback");

    if (wakeup)
        store_wakeup_dependent_loads(sob_entry.uop->alloc_id, WakeCode::ST_ADDR);

    uop_sta_early_complete_event(sob_entry.uop);
    complete_sta(sob_entry, writeback);
}

void LSU::complete_sta(SOBEntry& sob_entry, bool writeback, bool as_load)
{
    uop_sta_complete_event(sob_entry.uop);

    if (sob_entry.uop->is_store() && sob_entry.uop->is_exclusive() && !sob_entry.exclusive_second_pass)
        pmu_event(sob_entry.uop, pmu_num_data(archx::PMU::STREX_SPEC));

    // The life of a store exclusive:
    // 1) Wait until it is non-spec (next to commit).
    // 2) Execute for the first time, but when we get here don't write back, since we have to wait for the L2 response.
    //    Instead we set exclusive_complete which allows the store to retire even though it hasn't committed.
    // 3) Go down the retirement pipe and send the store to the L2, but do not deallocate the SOB entry.
    // 4) L2 sends a response when the store completes, which recycles the store, and sets exclusive_second_pass.
    // 5) The second time through, exclusive_second_pass is set so when we get here we allow the uop to write back.
    // 6) Store retirement sees the exclusive_second_pass flag and does not retire the uop a second time.
    // 7) When the uop commits, we deallocate the SOB entry.
    if (!can_complete_exclusive_store(sob_entry)) {
        sob_entry.exclusive_complete = true;
    } else if (can_complete_non_exclusive_store(sob_entry)) {
        if (writeback)
            writeback_store(sob_entry, sob_entry.uop, as_load);
        try_completing_store(sob_entry, sob_entry.uop, as_load);
    }
}

bool LSU::address_resident(PhysAddr addr) const
{
    return l1d_cache.address_resident(addr);
}

// Stat collection functions

void LSU::measure_load_execute_disorder(const Uop& uop)
{
    if (uop->lob_alloc_id_val != last_lob_id_executed + 1) {
        auto lob_distance = std::abs((long int)(uop->lob_alloc_id_val - last_lob_id_executed));

        ev_load_execute_disorder(uop, evd_load_disorder_distance((unsigned)lob_distance));
    }
    last_lob_id_executed = uop->lob_alloc_id_val;
}

void LSU::measure_load_miss_disorder(const Uop& uop)
{
    auto lob_alloc_id = uop->lob_alloc_id_val;
    if (lob_alloc_id < last_lob_id_miss_request) {
        ev_load_miss_disorder(uop, evd_load_disorder_distance((unsigned)(last_lob_id_miss_request - lob_alloc_id)));
    } else if (last_lob_id_miss_request < youngest_lob_id_miss_request && lob_alloc_id > youngest_lob_id_miss_request) {
        ev_load_miss_order_restore(uop);
    }
    if (lob_alloc_id > youngest_lob_id_miss_request) {
        youngest_lob_id_miss_request = lob_alloc_id;
    }
    last_lob_id_miss_request = lob_alloc_id;
}

// Utility functions

void LSU::print_lob(std::ostream& os) const
{
    uint idx{0};
    fmt::print(os, "\nCycle: {}; LOB size: {}\n", get_cycle(), lob.size());
    fmt::print(os, "\nCycle: {}; LUB size (combined): {}\n", get_cycle(), lub.size());
    if (!lob.empty()) {
        fmt::print(os,
                   "{1:<{0}} | {2:<{0}} | {3:<{0}} | {4:<{0}} | {5:<{0}} | {6:<{0}} | {7:<{0}}\n",
                   12,
                   "LOB idx",
                   "alloc_id",
                   "executing",
                   "complete",
                   "wake_code",
                   "wake_id",
                   "assigned_lob_id");
        for (const auto& lob_handle : lob) {
            fmt::print(os,
                       "{1:<{0}} | {2:<{0}} | {3:<{0}} | {4:<{0}} | {5:<{0}} | {6:<{0}} | {7:<{0}}\n",
                       12,
                       idx++,
                       lob_handle.first,
                       lob_handle.second.executing,
                       lob_handle.second.complete,
                       lob_handle.second.wake_code,
                       lob_handle.second.wake_id ? *lob_handle.second.wake_id : 0,
                       lob_handle.second.uop->lob_id_val);
        }
    }
}

void LSU::print_sob(std::ostream& os) const
{
    uint idx{0};
    fmt::print(os, "\nSOB size: {}\n", sob.size());

    if (!sob.empty()) {
        fmt::print(os,
                   "{1:<{0}} | {2:<{0}} | {3:<{0}} | {4:<{0}} | {5:<{0}} | {6:<{0}} | {7:<{0}} | {8:<{0}} | "
                   "{9:<{0}}\n",
                   15,
                   "SOB idx",
                   "alloc_id",
                   "executing",
                   "addr_complete",
                   "data_low_complete",
                   "data_high_complete",
                   "stp",
                   "wake_code",
                   "committed");
        for (const auto& sob_handle : sob) {
            fmt::print(os,
                       "{1:<{0}} | {2:<{0}} | {3:<{0}} | {4:<{0}} | {5:<{0}} | {6:<{0}} | {7:<{0}} | {8:<{0}} | "
                       "{9:<{0}}\n",
                       15,
                       idx++,
                       sob_handle.first,
                       sob_handle.second.executing,
                       sob_handle.second.addr_complete,
                       sob_handle.second.data_low_complete,
                       sob_handle.second.data_high_complete,
                       sob_handle.second.pair,
                       sob_handle.second.wake_code,
                       sob_handle.second.committed);
        }
    }
}

void LSU::dump(std::ostream& os) const
{
    fmt::print(os, "= LSU ======================================================\n");
    print_lob(os);
    print_sob(os);
    fmt::print(os, "============================================================\n");
}

} // namespace ampere::core

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
