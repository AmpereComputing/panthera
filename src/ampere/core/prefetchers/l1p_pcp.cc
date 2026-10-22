//
// Copyright (c) 2018-2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/core/prefetchers/l1p_pcp.h"

#include "ampere/archx/addr.h"
#include "ampere/archx/el.h"
#include "ampere/archx/pmu.h"
#include "ampere/core/backend_prefetcher.h"
#include "ampere/core/prefetcher.h"

#include <optional>
#include <string>

// #define DEBUG_PCP
#if defined(DEBUG_PCP)
#define DBG_PCP(format_str, ...) fmt::print(stderr, "{}: " format_str "\n", get_cycle(), ##__VA_ARGS__)
#else
#define DBG_PCP(format_str, ...)
#endif

namespace ampere::core {

bool PCP::can_allocate_pob(BackendMemReq req, archx::EL current_level) const
{
    return recycle_tlb_miss;
}

/*************************************************************************
 * On an event of a train, this function is used to insert or lookup the
 * entry for that pc in the stride table
 *************************************************************************/
PCP::Table::Result PCP::Table::lookup_or_insert(const BackendMemReq &request)
{
    if (auto hit = lookup(request.transaction, request.pc); hit)
        return hit;

    if (auto victim = refill(request.transaction, request.pc, PCP::Entry(PCP_SEARCH, request.pc)); victim) {
        if (victim->state == PCP_ACTIVE_LO)
            act_lo_cnt--;
        if (victim->state == PCP_ACTIVE_HI)
            act_hi_cnt--;
    }
    DBG_PCP("insert of uop pc 0x{:x} in idx={}", request.pc, compute_set(request.pc));
    return lookup(request.transaction, request.pc, false, true);
}

bool PCP::should_trigger(const Entry &entry) const
{
    return entry.state == PCP_ACTIVE_HI || (prefetch_in_active_lo && entry.state == PCP_ACTIVE_LO);
}

/*************************************************************************
 * Stride was found in active hi state, so here is the algorithm to
 * put a new request onto the on-deck circle.
 *************************************************************************/
void PCP::trigger_pf(const BackendMemReq &request)
{
    // figure out which entry we need to prefetch for
    unsigned idx = table.compute_set(request.pc);

    DBG_PCP("pf trigger of uop pc 0x{:x} in idx={}", request.pc, set);

    // look up the entry
    auto hit = table.lookup(request.transaction, request.pc);
    if (!hit)
        AMPERE_ERROR("triggering pf for uop that was not trained in the table {}\n", idx);
    auto &e = *hit;

    // Check and make sure confidences is high before putting opportunistic load on deck
    if (!should_trigger(e))
        return;

    // calculate next address.
    auto offset = e.offset();
    // Furiosa has a bug which truncates the offset to 14 bits (plus a sign bit) after applying the multiplier
    if (truncate_offset_bug) {
        offset &= ((1 << 15) - 1);
        if (offset & 0x4000)
            offset |= 0xffff8000;
    }
    archx::VirtAddr next_vaddr = request.virt_addr + offset;

    // The upper bits of the VA were always zero-extended, so prefetch triggers in TTBR1 space
    // resulted in a non-canonical address, which looks like a fault and gets dropped.
    if (upper_va_bits_bug && next_vaddr.extract_range(63, 63)) {
        next_vaddr &= ~(1ULL << 63);
        next_vaddr |= 1ULL << 62;
    }

    // Record event when we cross the page boundary
    if (!request.virt_addr.same_page_4k(next_vaddr)) {
        ev_page_cross(request.transaction);
        if (!page_cross_4k)
            return;
    }

    // All cores check whether the generated prefetch is a duplicate of anything in the on-deck queue
    for (unsigned i = 0; i < prefetch_request->size(); ++i)
        if (prefetch_request->get(i).virt_addr.same_cache_line(next_vaddr))
            return;

    // We can also avoid generating the same prefetch that this entry generated last time
    if (remove_dup && e.last_prefetched_vaddr.same_cache_line(next_vaddr))
        return;

    BackendMemReq pf(next_vaddr, {}, request, this->prefetch_type());

    if (!prefetch_request->can_put()) {
        overflow(pf);
        return;
    }

    // aliased and ev_aliased_trigger are purely for events. This code doesn't change performance
    bool aliased = (request.pc.line_address() != e.last_pc.line_address())
                   || (request.pc.line_address() != e.last_last_pc.line_address());
    if (aliased) {
        ev_aliased_trigger(request.transaction,
                           pf.virt_addr,
                           request.pc,
                           ev_stride(e.stride),
                           ev_stride_multiplier(e.stride_mult),
                           ev_pcp_index(idx),
                           ev_pcp_way(hit.way()),
                           ev_pcp_pc(e.pc.value()),
                           ev_stride(e.stride));
    }

    e.last_prefetched_vaddr = next_vaddr;

    if (request.uop)
        request.uop->set_generated_L1_prefetch();

    // Place request in circular queue. This will overwrite the oldest
    // entry in the queue if it did not get sent out.
    // consumer_pc: prefetched data is expected to be consumed by this same PC in a few strides
    pf.exception_level = std::nullopt;
    prefetch_request->put(pf);

    ev_trigger(request.transaction,
               pf.virt_addr,
               request.pc,
               ev_stride(e.stride),
               ev_stride_multiplier(e.stride_mult),
               ev_pcp_index(idx),
               ev_pcp_way(hit.way()),
               ev_pcp_pc(e.pc.value()),
               ev_stride(e.stride));
}

/*************************************************************************
 * Algorithm and state machine for what happens on each training event.
 * Each demand load trains the PCP here.
 *************************************************************************/
void PCP::train_impl(BackendMemReq request, bool hit, bool prefetch, bool demand)
{
    // figure out which entry to update
    unsigned idx = table.compute_set(request.pc);

    DBG_PCP("train of uop pc 0x{:x} in idx={}", request.pc, idx);

    // figure out the stride
    auto entry = table.lookup_or_insert(request);
    auto &e = *entry;
    int stride = (request.virt_addr & addr_mask) - (e.last_vaddr & addr_mask);
    unsigned way = entry.way();

    // FIXME: only works for up to 2 trains per cycle
    // Can't update the same table set twice in one cycle
    if (idx == last_train_idx && way == last_train_way && get_cycle() == last_train_cycle) {
        ev_training_dropped(request.transaction);
        return;
    }

    last_train_idx = idx;
    last_train_way = way;
    last_train_cycle = get_cycle();

    // If stride is zero, we don't update the state machine but we still generate prefetches
    // (which will normally be squashed by the duplicate detection)
    if (stride != 0) {
        // was it a stride match?
        bool stride_match = (e.stride == stride);
        if (out_of_order_stride_match)
            stride_match =
                stride_match || (e.state == PCP_ACTIVE_HI && (stride == -e.stride || stride == 2 * e.stride));

        e.last_vaddr = request.virt_addr;
        e.push_stride_alloc_id(request.alloc_id);

        // execute the state machine
        switch (e.state) {
        // INIT goes to SEARCH on the first train
        case PCP_INIT:
            e.stride = stride;
            e.state = PCP_SEARCH;
            break;

        case PCP_SEARCH:
            if (stride_match) {
                // SEARCH goes to ACTIVE_HI on a stride confirmation
                e.state = (train_into_active_lo ? PCP_ACTIVE_LO : PCP_ACTIVE_HI);
                if (train_into_active_lo)
                    table.act_lo_cnt++;
                else
                    table.act_hi_cnt++;
                if (should_trigger(e))
                    ev_pmu(request.transaction, ev_pmu_num(archx::PMU::L1_PREFETCH_ENTRY_TRAINED));
            } else {
                // SEARCH remains on a stride miss but resets stream
                e.stride = stride;
            }
            break;

        case PCP_ACTIVE_HI:
            if (stride_match) {
                // ACTIVE_HI remains on a stride confirmation
                // communicate to Gretch this specific PC
                if (signal_training)
                    training->train(request, StrideTrainingMethod::STABLE_HI);
            } else {
                // ACTIVE_HI on a miss goes to ACTIVE_LO to dampen noise
                e.state = PCP_ACTIVE_LO;
                table.act_hi_cnt--;
                table.act_lo_cnt++;
                if (multiplier_decrease_on_stride_mismatch) {
                    if (e.stride_mult > 2) {
                        e.stride_mult >>= 1;
                        ev_stride_mult_dec(request.transaction,
                                           ev_stride_multiplier(e.stride_mult),
                                           ev_pcp_index(idx),
                                           ev_pcp_way(way),
                                           ev_pcp_pc(e.pc.value()),
                                           ev_stride(stride));
                    }
                }
                if (update_stride_on_active_lo)
                    e.stride = stride; // Not clear this is desired, but it's what RTL does -- fixed on Ripley

                // communicate to Gretch this specific PC
                if (signal_training)
                    training->train(request, StrideTrainingMethod::HI_TO_LO);
            }
            break;

        case PCP_ACTIVE_LO:
            if (stride_match) {
                // ACTIVE_LO goes to ACTIVE_HI if we hit the old stride again
                e.state = PCP_ACTIVE_HI;
                table.act_hi_cnt++;
                table.act_lo_cnt--;
                if (should_trigger(e))
                    ev_pmu(request.transaction, ev_pmu_num(archx::PMU::L1_PREFETCH_ENTRY_TRAINED));

                // communicate to Gretch this specific PC
                if (signal_training)
                    training->train(request, StrideTrainingMethod::LO_TO_HI);
            } else {
                // ACTIVE_LO goes to SEARCH if there are two wrong strides
                e.stride = stride;
                e.stride_mult = 2;
                e.state = PCP_SEARCH;
                table.act_lo_cnt--;
                ev_stride_mult_rst(request.transaction,
                                   ev_pcp_index(idx),
                                   ev_pcp_way(way),
                                   ev_pcp_pc(e.pc.value()),
                                   ev_stride(stride),
                                   ev_stride_multiplier(e.stride_mult),
                                   ev_allocation_distance(e.allocation_distance()));
                ev_pmu(request.transaction, ev_pmu_num(archx::PMU::L1_PREFETCH_DIST_RST), ev_pcp_index(idx));
            }
            break;

        default:
            assert(0 && "bad state in PCP::train()");
            break;
        }

        ev_train(request,
                 request.virt_addr,
                 ev_pcp_index(idx),
                 ev_pcp_way(way),
                 ev_pcp_pc(e.pc.value()),
                 ev_stride(stride),
                 ev_stride_multiplier(e.stride_mult));
    }

    // issue prefetch
    if (!(stride == 0 && drop_zero_stride_prefetches))
        trigger_pf(request);
    // last PC only for aliasing_event. We do this after training on the pc
    e.update_last_pc(request.pc);

    table.max_act_hi = table.act_hi_cnt > table.max_act_hi ? table.act_hi_cnt : table.max_act_hi;
    table.max_act_lo = table.act_lo_cnt > table.max_act_lo ? table.act_lo_cnt : table.max_act_lo;
}

void PCP::execute_impl(
    uint64_t index, BackendMemReq req, bool hit, bool prefetch, bool demand, PrefetchType prefetch_type)
{
    if (hit || !demand || !prefetch)
        return;

    // If we hit an OFB which was allocated by PCP or Gretch, increase the stride multiplier of the target entry in PCP
    // table
    bool aggressive = false;
    if (signal_training)
        aggressive = training->should_inc_multiplier(prefetch_type);

    auto *in_flight_req = get_inflight(req);
    if (in_flight_req && should_inc_stride_mult(prefetch, in_flight_req->stride_mult_increase)) {
        in_flight_req->stride_mult_increase = stride_mult_once_per_ofb;

        // Gretch OFB is allocated by producer PC but the demand load is for consumer PC.
        // for PCP, demand load PC = PC which triggered the prefetch
        // We want to increase stride mult of producer PC in PCP table
        stride_mult_inc(req, in_flight_req->pc, aggressive);
    }
}

void PCP::drop_impl(BackendMemReq req, PrefetchDropReason reason)
{
    if (inc_on_ofb_hit
        && (reason == PrefetchDropReason::CACHE_MISS_IN_FLIGHT
            || reason == PrefetchDropReason::CACHE_MISS_PREFETCH_IN_FLIGHT))
    {
        bool prefetch = reason == PrefetchDropReason::CACHE_MISS_PREFETCH_IN_FLIGHT;
        auto &in_flight_req = add_inflight(req);
        // If a PCP prefetch hits a non-prefetch OFB, the prefetch was late -- increase the multiplier
        if (should_inc_stride_mult(!static_cast<bool>(prefetch), in_flight_req.stride_mult_increase)) {
            in_flight_req.stride_mult_increase = stride_mult_once_per_ofb;
            stride_mult_inc(req, in_flight_req.pc, false);
        }
    }
}

void PCP::launch_impl(BackendMemReq req)
{
    add_inflight(req);
}

void PCP::launch_related(BackendMemReq req)
{
    if (training->should_inc_multiplier(req.prefetch_type))
        add_inflight(req);
}

void PCP::fill_impl(uint64_t index, BackendMemReq req, bool fb_pref, bool was_alloc_by_pf)
{
    remove_inflight(req);
}

bool PCP::should_inc_stride_mult(bool prefetch, bool done_already) const
{
    return enable_adaptive_stride_mult && prefetch && !done_already;
}

/*************************************************************************
 * Attempt increase the stride multiplier
 ************************************************************************/
void PCP::stride_mult_inc(BackendMemReq request, ProgramCounter pc, bool aggressive)
{
    // get index of which entry we are going to increase the multiplier
    unsigned idx = table.compute_set(pc);

    // look up the entry
    auto hit = table.lookup(request.transaction, pc);
    if (!hit) {
        ev_aliased_evicted(request.transaction);
        return;
    }
    auto &e = *hit;
    unsigned way = hit.way();

    // If we are using 'enable_slow_stride_increase', only increase the
    // multiplier if we've seen a number of increase events exceeding the
    // current multiplier since the counter was last reset.
    if (aggressive || !enable_slow_stride_increase || stride_mult_inc_called >= e.stride_mult
        || stride_mult_inc_called >= max_slow_increase_ctr)
    {
        if (e.stride_mult != stride_mult_max) {
            e.stride_mult *= 2;
            if (e.stride_mult > stride_mult_max) {
                e.stride_mult = stride_mult_max;
            }
            ev_stride_mult_inc(request.transaction,
                               ev_stride_multiplier(e.stride_mult),
                               ev_pcp_index(idx),
                               ev_pcp_way(way),
                               ev_pcp_pc(e.pc.value()),
                               ev_stride(e.stride));

            // Patch holes in prefetcher pattern from increasing the stride multiplier
            if (fill_hole_on_mult_inc) {
                auto req = request;
                req.pc = pc;
                trigger_pf(req);
            }
        }

        stride_mult_inc_called = 0;
    } else {
        stride_mult_inc_called += 1;
    }
}

void PCP::dump(std::ostream &os) const
{
    fmt::print(os, "= LSU PCP ==================================================\n");
    table_print(os);
    fmt::print(os, "============================================================\n");
}

/*************************************************************************
 * Function for observability. This is basically a watch window for the
 * PCP entries for inspecting the state machine for each index.
 *************************************************************************/
void PCP::table_print(std::ostream &os) const
{
    unsigned idx{0};

    unsigned act_hi{0}, act_lo{0};

    fmt::print(os,
               "\n{1:>{0}} | {2:>{0}} | {3:>{9}} | {4:>{0}} | {5:>{0}} | {6:>{0}} | {7:>{9}} | {8:>{9}}\n",
               7,
               "idx",
               "way",
               "pc",
               "state",
               "stride",
               "str_mlt",
               "last_vaddr",
               "alloc_id(0, 1)",
               15);
    for (const auto &set : this->table.get_sets()) {
        for (const auto &it : set) {
            const auto &pcp_entry = it.second.value;
            // Convert enum to string
            std::string state_str;
            switch (pcp_entry.state) {
            case PCP_INIT:
                state_str = "INIT";
                break;
            case PCP_SEARCH:
                state_str = "SEARCH";
                break;
            case PCP_ACTIVE_HI:
                state_str = "ACT_HI";
                act_hi++;
                break;
            case PCP_ACTIVE_LO:
                state_str = "ACT_LO";
                act_lo++;
                break;
            default:
                state_str = "UNKNOWN";
            }
            // Build alloc_id string
            std::string stride_id = "(" + std::to_string(pcp_entry.stride_alloc_id[0]) + ", "
                                    + std::to_string(pcp_entry.stride_alloc_id[1]) + ")";

            fmt::print(os,
                       "{1:>{0}} | {2:>{0}} | {3:>#{9}x} | {4:>{0}} | {5:>{0}} | {6:>{0}} | {7:>{9}} | {8:>{9}}\n",
                       7,
                       idx,
                       it.second.way,
                       pcp_entry.pc.value(),
                       state_str,
                       pcp_entry.stride,
                       pcp_entry.stride_mult,
                       pcp_entry.last_vaddr,
                       stride_id,
                       15);
        }
        idx++;
    }
    fmt::print(os, "\nPCP COUNTS: ACTIVE_HI = {}, ACTIVE_LO = {}\n", act_hi, act_lo);
}

void PCP::end_of_elaboration()
{
    signal_training = training.bind_count() > 0;
}

/*************************************************************************
 * end of simulation, print out final stat
 *************************************************************************/
void PCP::end_of_simulation()
{
    Module::end_of_simulation();

    for (unsigned i = 0; i < table.max_act_hi; i++)
        ev_active_hi_entries();
    for (unsigned i = 0; i < table.max_act_lo; i++)
        ev_active_lo_entries();
}

L1P_PCP::L1P_PCP(const sc_core::sc_module_name &name) : PCP(name)
{
    connect(pipe).name("pipe");
}

bool L1P_PCP::run()
{
    // Train and trigger prefetch. uarch trains PCP in LD04 and
    // writes on-deck in LD05 but we do it all in one cycle (LD05).
    while (pipe.can_take())
        train(pipe.take()); // XU07/LD04

    return true;
}

void L1P_PCP::issue_impl(uint64_t id, BackendMemReq req)
{
    if (!ignore_other_loads_in_instruction || !req.uop || req.uop->first_mem_uop) {
        AMPERE_ASSERT(pipe.can_put(), "No bandwidth to train PCP on incoming load");
        pipe.put(req);
    }
}

void L1P_PCP::flush_impl(Flush flush_info)
{
    pipe.clear_if([&](const auto &e) { return e.uop && e.alloc_id >= flush_info->alloc_id; });
}

bool L1P_PCP::commit_impl(uint64_t id, BackendMemReq req)
{
    pipe.clear_if([&](const auto &e) { return e.uop && e.alloc_id == id; });
    return true;
}

} // namespace ampere::core

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
