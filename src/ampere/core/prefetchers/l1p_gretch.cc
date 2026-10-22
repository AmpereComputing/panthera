//
// Copyright (c) 2021-2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/core/prefetchers/l1p_gretch.h"

#include "ampere/archx/el.h"
#include "ampere/archx/page_size.h"
#include "ampere/archx/pmu.h"
#include "ampere/core/backend_prefetcher.h"
#include "ampere/core/prefetchers/l1p_pcp.h"
#include "ampere/sc/ancestor.h"
#include "ampere/sc/report.h"

#include <optional>
#include <string>
#include <fmt/core.h>
// #define DEBUG_GRETCH
#if defined(DEBUG_GRETCH)
#define DBG_GRCH(format_str, ...) fmt::print(stderr, "{}: " format_str "\n", fmt_cycle(), ##__VA_ARGS__)
#else
#define DBG_GRCH(format_str, ...) \
    do {                          \
    } while (0)
#endif

namespace ampere::core {

GRETCH::GRETCH(const sc_core::sc_module_name& name)
    : BackendPrefetcher<BackendMemReq>(name),
      pcp_training(pcp_communication, data_bw, aggressive_pcp_control),
      drt(drt_size)
{
    AMPERE_ASSERT(confidence_threshold >= 1, "Confidence threshold must be at least 1");

    // introduce latency for 1) invalidating DRT entries, 2) populating ADT, and 3) measuring confidence in RT
    connect(ld_commit).name("ld_commit_pipe").latency(1);
    // introduce latency for 1) staging 2) performing C + D * data 3) overflow check
    connect(gen_pref).name("gen_pref_pipe").bandwidth(data_bw).latency(3);
}

void GRETCH::update_pcp_state_change_pc()
{
    std::optional<BackendMemReq> pth_req;
    while (auto req = pcp_training.gather()) {
        if (!meets_security_policy(*req))
            continue;

        // Only write one PC into PTH per cycle:
        // 1) If only one PC is generated, input to PTH
        // 2) If more than one PC is generated, input to PTH the one coming from pipe0
        if (!pth_req || req->pipe < pth_req->pipe)
            pth_req = req;
    }

    // only insert EL0 PCs in PTH; full PC passed in, PC[10:2] extracted in function defined in gretch.cc;
    if (pth_req && update_pth(pth_req->pc))
        ev_pth_input(*pth_req, pth_req->pc);
}

bool GRETCH::run()
{
    // Gretch runs after any PCP training
    update_pcp_state_change_pc();

    if (dynamic_adjust_training)
        manage_dynamic_training();
    if (force_insert)
        force_inject_in_rt(); // only for PV purposes

    train_gen_prefetch();
    train_supply_cacheline_data();
    train_adt_populate_measure_confidence();
    train_adt_compute();

    return true;
}

// Stages each data item in cacheline
void GRETCH::train_supply_cacheline_data()
{
    for (unsigned i = 0; i < data_bw; i++) {
        if (!cacheline_buffers.empty()) {
            process_and_supply_cacheline_data();
        }
    }

    while (gen_pref.can_put() && !supplied_cl_data.empty()) {
        GRETCH::CLDataSlice cl_data_slice = supplied_cl_data.front();
        supplied_cl_data.pop_front();
        gen_pref.put(cl_data_slice);

        // If RT has 2 hit high-confidence producers, stop here
        // because the same data will be used to generate >1 prefetch addresses.
        if (current_cacheline_num_rt_hits > 1)
            return;
    }
}

// handle committed loads to invalidate DRT, populate ADT, and measure confidence in RT
void GRETCH::train_adt_populate_measure_confidence()
{
    unsigned num_commits = 0;

    while (ld_commit.can_take()) {
        auto req = ld_commit.take();
        num_commits++;
        bool use_for_gretch_training = num_commits <= commit_bw;
        auto [data, data_valid] = handle_load_committed_from_lsu(req, use_for_gretch_training);

        if (use_for_gretch_training)
            update_confidence_at_commit(req, data, data_valid);
    }
}

/* complete ADT pair computations */
void GRETCH::train_adt_compute()
{
    if (within_security_policy() && !comp_queue.empty()) {
        GRETCH::CompInfo ci = comp_queue.front();
        if (get_cycle() >= ci.complete_cycle) {
            bool rf = ci.rt_table_temp_index > -1;
            ev_adt_train(
                get_adt_train_req(), evd_relationship_found(rf), evd_hl(ci.high_latency), evd_comp_id(ci.comp_id));
            update_rt(ci.rt_table_temp_index, ci.comp_id);
            comp_queue.pop_front();
        }
    }
}

/* call Gretch's internal function to generate prefetches */
void GRETCH::train_gen_prefetch()
{
    pref_this_cycle = 0;
    while (gen_pref.can_take()) {
        auto cl_data_slice = gen_pref.take();
        generate_prefetches(cl_data_slice);
    }
}

void GRETCH::issue_impl(uint64_t id, BackendMemReq req)
{
    if (use_drt && meets_security_policy(req)) {
        // In gretch.cc, current_el_gpc_stage is registered in GPC stage when Gretch sees a committing load.
        // By the time gretch.cc registers an EL change in the GPC stage, an EL1 uop may be in the issue stage and may
        // allocate LOB Id in DRT. To prevent that, we have the security condition here.
        manage_drt(GRETCH::DRTOpType::ALLOCATE_LOB_ID, id, req.memory_buffer_id, req.pc, req.uop);
    }
}

void GRETCH::writeback_impl(uint64_t id, BackendMemReq req)
{
    if (use_drt && meets_security_policy(req)) {
        // In gretch.cc, current_el_gpc_stage is registered in GPC stage when Gretch sees a committing load.
        // By the time gretch.cc registers an EL change in the GPC stage, an EL1 uop may be in the load_complete stage
        // and may write data in DRT. To prevent that, we have the security condition here.
        manage_drt(GRETCH::DRTOpType::WRITE_DATA, id, req.memory_buffer_id);
    }
}

bool GRETCH::commit_impl(uint64_t id, BackendMemReq req)
{
    if (ld_commit.can_put() && req.phys_addr)
        ld_commit.put(req);

    return !guaranteed_training || inactive() || ld_commit.can_put();
}

void GRETCH::fill_impl(BackendMemReq req, bool fb_pref, bool was_alloc_by_pf)
{
    if (req.alloc_id > 0 && req.phys_addr
        && (req.prefetch_type == PrefetchType::PCP
            || (!fb_pref && !was_alloc_by_pf && req.prefetch_type == PrefetchType::NONE)))
    {
        unsigned size = req.data_size >= 8 ? 8 : 4;
        buffer_incoming_fill(req, *req.phys_addr, size, !fb_pref);
    }
}

void GRETCH::flush_impl(Flush flush_info)
{
    if (use_drt)
        manage_drt(GRETCH::DRTOpType::INVALIDATE, flush_info->alloc_id, flush_info->memory_buffer_id);
}

bool GRETCH::can_allocate_pob(BackendMemReq request, archx::EL current_level) const
{
    return recycle_tlb_miss && current_level == archx::EL0;
}

bool GRETCH::can_issue_prefetch() const
{
    return current_el_gpc_stage == archx::EL0;
}

/*
 * Check if ADT is ready to start a new round of ADT population.
 * If yes, start the first ADT entry and mark ADT as busy-populating.
 * Inputs:
 * u: provides <PC, virt_addr>
 * data_val: data value queried at commit time
 * Output:
 * return true if a new round of ADT population started
 */
bool GRETCH::activate_adt_populate_at_commit(BackendMemReq req, uint64_t data_val)
{
    AMPERE_ASSERT(!inactive(), "ADT should not be triggered when gated and when privilege level is higher than EL0");

    auto pc = ixu_pc(req.pc); // here, PC is only for events
    auto vaddr = req.virt_addr;

    if ((state == GretchState::WAIT_TRIGGER) || (state == GretchState::INIT)) {
        AMPERE_ASSERT(num_addr_observed == 0,
                      "Gretch is trying to start a new round of ADT population but commit threshold counter is not "
                      "correctly reset.");
        flush_adt(); // clear adt contents if previously populated
        state = GretchState::POPULATE_ADT;
        ev_adt_trigger(req, pc, VirtAddr(vaddr.extract_range(52, 0)));
        pmu_event(req, pmu_num_data(archx::PMU::L1_PREFETCH_GR_ADT_TRIGGER));
        populate_adt_at_commit(
            req, data_val, true /*since ADT is being triggered, data must have been found at commit stage*/);
        return true;
    }

    return false;
}

/*
 * Populate ADT entries with <PC, virt_addr, data>.
 * If an ADT entry already contains a matching PC and there is space, only insert <virt_addr, data> for that PC.
 * If there is no matching PC and there are free ADT entries, use a new entry.
 * If threshold number of addresses encountered or ADT is full, mark ADT as busy-training to prepare
 * for performing div operations on ADT pairs.
 * Inputs:
 * u: provides <PC, virt_addr>
 * data_val: data value queried at commit time
 * data_found: whether data was found in DRT
 */
void GRETCH::populate_adt_at_commit(BackendMemReq req, uint64_t data_val, bool data_found)
{
    if (state != GretchState::POPULATE_ADT)
        return;

    if (!within_security_policy()) // pause in non-EL0
        return;

    AMPERE_ASSERT(!gated, "ADT should not be populated when gated");

    num_addr_observed++; // counter -> keep track if threshold is reached

    auto pc = ixu_pc(req.pc);
    auto vaddr = req.virt_addr;
    bool hit = false;

    // check pc across all entries to find a hit
    int index = 0;
    for (ADTEntry& adt_entry : adt_table) {
        // FIXME: should multiple hits be allowed?
        if (adt_entry.pc == pc) {
            hit = true;
            AMPERE_ASSERT(adt_entry.num_addresses != 0, "PC hits GRETCH ADT but no addresses in ADT for the PC.");
            if (adt_entry.num_addresses == 1) {
                adt_entry.addr[1] = vaddr;
                adt_entry.data[1] = data_val;
                adt_entry.num_addresses++;
                if ((index == 0) && data_found) {
                    adt_entry.num_data++; // Only first ADT entry uses num_data
                }

                // for debugging
                adt_entry.lobid[1] = req.memory_buffer_id;
                adt_entry.inum[1] = req.uop ? req.uop->inum() : 0;
            }
        }

        index++;
    }

    // check if it is time to train on ADT contents
    if (adt_full() || (num_addr_observed == commit_threshold)) {
        ev_adt_epoch(req, evd_adt_timeout(num_addr_observed == commit_threshold));
        num_addr_observed = 0; // reset number of observed addresses
        state = GretchState::TRAIN;
        train_on_adt_contents(req);
        return;
    }

    // make new entry in ADT
    if (!hit && (adt_table.size() < adt_size)) {
        ADTEntry ae;
        ae.pc = pc;
        ae.addr[0] = vaddr;
        ae.data[0] = data_val;
        ae.num_addresses = 1;

        if (adt_table.empty()) {
            // Only first ADT entry uses num_data
            ae.num_data = 1;
        }
        // for debugging
        ae.lobid[0] = req.memory_buffer_id;
        ae.inum[0] = req.uop ? req.uop->inum() : 0;

        adt_table.push_back(ae);
    }
}

/*
 * Invalidate all ADT entries before starting a new round of ADT population
 */
void GRETCH::flush_adt()
{
    adt_table.clear();
}

bool GRETCH::adt_full()
{
    if (adt_table.size() < adt_size)
        return false;

    for (const ADTEntry& adt_entry : adt_table) {
        if (adt_entry.num_addresses < 2)
            return false;
    }

    return true;
}

/*
 * Find if there exists an array-indirect relationship between ADT entries a and b.
 * Inputs:
 * a: ADT_entry[0].
 * b: ADT_entry[i] where i is not equal to 0.
 * base_addr: base address found as the outcome of divider operation.
 * offset: offset found as the outcome of divider operation.
 *    <base_addr, offset> tuple defines an array-indirect data-dependency relationship.
 * early_exit: whether this computation turns out to be high latency division (early_exit = false)
 * or division is somehow skipped (early_exit = true).
 * Output:
 *  return whether an array-indirect relationship is found.
 */
bool GRETCH::infer_array_indirect(
    const ADTEntry& a, const ADTEntry& b, uint64_t& base_addr, uint64_t& offset, bool& early_exit)
{
    /* ADTEntry a should be 0th entry where data is stored. b does not store data */
    bool a_full = (a.num_addresses == MAX_ADDRESSES) && (a.num_data == MAX_ADDRESSES);
    bool b_full = (b.num_addresses == MAX_ADDRESSES);

    // SKIP DIVISION
    if (!(a_full && b_full)) {
        early_exit = true;
        return false;
    }

    /*
    Solve simulataneous equations:
    addr0 = base_addr + offset * data0
    addr1 = base_addr + offset * data1

    Unknowns: base_addr, offset
    addr0, addr1 belong to ADTEntry b
    data0, data1 belong to ADTEntry a
    */

    // absolute data difference in the first entry (i.e., a)
    uint64_t data0 = a.data[0];
    uint64_t data1 = a.data[1];
    uint64_t data_diff = (data0 > data1) ? (data0 - data1) : (data1 - data0);

    // absolute address difference in the second entry (i.e. b)
    uint64_t addr0 = b.addr[0].value();
    uint64_t addr1 = b.addr[1].value();
    uint64_t addr_diff = (addr0 > addr1) ? (addr0 - addr1) : (addr1 - addr0);

    // SKIP DIVISION
    if (addr_diff < data_diff) {
        // example: <addr_diff, data_diff> = <4, 3555745>. This DIV will provide <0 offset, so not needed.
        early_exit = true;
        return false;
    }

    // shorten operand size based on param
    unsigned lo = 0;
    unsigned hi = div_bitwidth - 1;
    addr_diff = (Bitfield(addr_diff)).extract_range(hi, lo);
    data_diff = (Bitfield(data_diff)).extract_range(hi, lo);

    // SKIP DIVISION
    // After truncating, some addr_diff become 0, leading to offset = 0 (example below). 0 offset has no physical
    // meaning. Example (with 8-bit divider): Truncated addr_diff/data_diff: (0x0, 0x3)
    //                               Original addr_diff/data_diff: (0x2d34bba00, 0x3)
    if ((addr_diff == 0) || (data_diff == 0)) {
        early_exit = true;
        return false;
    }

    /* From here on, we perform at least mod calculation. Hence, these are counted as divs.
       So, from here on, nothing is categorized as early exit. */
    ev_div();

    if (addr_diff % data_diff == 0) {
        uint64_t off = addr_diff / data_diff;
        if (off > OFFSET_THRESHOLD) {
            early_exit = false;
            return false;
        }
        offset = off;
        base_addr = addr1 - offset * data1;
        early_exit = false;
        return true;
    }

    // Remaining possibility: Mod is non-zero
    early_exit = false;
    return false;
}

/*
 * Find if there exists array-indirect relationships between ADT[0] and other ADT entries.
 * All the ADT pair computations are done at once and staged/saved temporarily. Each computation is allowed to
 * complete (i.e., marked as done) after the right number of cycles (corresponding to the operation latency) have
 * elapsed. Note: this function is mimicking real hardware: ADT pair computations happen one after the other. Outcome of
 * each ADT pair computation undergoes the following (none of which happens in real hardware, i.e., there is no
 * CompInfo, comp_queue, rt_table_temp in real hardware). This function only temporarily saves all the computation
 * results, allowing each one to finish after the correct number of cycles: latency saved in CompInfo and stored in
 * comp_queue. a successful data-dependency is temporarily stored in rt_table_temp. Input: u: helps track transaction in
 * event stream.
 */
void GRETCH::train_on_adt_contents(BackendMemReq req)
{
    if (state != GretchState::TRAIN)
        return;

    AMPERE_ASSERT_ERROR(!gated, "ADT Training should not happen while Gretch training is gated");

    int used_adt_entries = adt_table.size();

    if (used_adt_entries <= 1) {
        state = GretchState::WAIT_TRIGGER;
        return;
    }

    AMPERE_ASSERT(
        comp_queue.empty() && (num_comp_remaining == 0),
        "New round of ADT train is not possible. There are remnant pair-wise computations from previous round.");

    // reset before ADT pair computations
    last_rt_table_temp_index = -1;
    rt_table_temp.clear();
    adt_content_train_req = req;

    // counters for event data
    uint64_t num_comps = 0;          // number of ADT pair computation results
    uint64_t num_comps_high_lat = 0; // number of ADT pair computations which require high latency (due to div))
    uint64_t num_comps_rt = 0; // number of ADT pair computations which find data-dependency relationship to update RT

    int comp_id = 0;

    auto complete_cycle = get_cycle(); // keep track of completion cycle of ADT pair computation

    // Look for array-indirect relationship between first entry and others
    for (int j = 1; j < used_adt_entries; j++) {
        num_comps++;
        uint64_t base_addr = 0;
        uint64_t offset = 0;
        bool early_exit = false;
        if (infer_array_indirect(adt_table[0], adt_table[j], base_addr, offset, early_exit)) {
            rt_table_temp.emplace_back(adt_table[0].pc, adt_table[j].pc, base_addr, offset);
            last_rt_table_temp_index++;
            complete_cycle += long_lat;
            if (j == 1) {
                complete_cycle += 1; // initial 1 cycle ADT state-change latency
                if (filter_close_base_addrs)
                    complete_cycle +=
                        1; // Filter nth training instance in parallel with starting training on the (n+1)th instance.
                           // So the overall training time of the full ADT would just be increased by 1 cycle.
            }
            comp_queue.push_back(CompInfo(comp_id, true /* high lat comp*/, last_rt_table_temp_index, complete_cycle));
            comp_id++;
            num_comps_rt++;
            num_comps_high_lat++;
        } else {
            if (early_exit)
                complete_cycle += low_lat;
            else
                complete_cycle += long_lat;
            if (j == 1) {
                complete_cycle += 1; // initial 1 cycle ADT state-change latency
                if (filter_close_base_addrs)
                    complete_cycle +=
                        1; // Filter nth training instance in parallel with starting training on the (n+1)th instance.
                           // So the overall training time of the full ADT would just be increased by 1 cycle.
            }
            comp_queue.push_back(
                CompInfo(comp_id, !early_exit /* if early_exit, not a high lat comp*/, complete_cycle));
            comp_id++;
            if (!early_exit)
                num_comps_high_lat++;
        }
    }

    ev_comp_queue(req, evd_num_comp(num_comps), evd_num_comp_hl(num_comps_high_lat), evd_num_comp_rt(num_comps_rt));

    num_comp_remaining = num_comps;
}

/*
 * Insert a data-dependency relationship in RT.
 * Replacement policy:
 * Layer 1: Evict the first entry with confidence <= threshold.
 * Layer 2: If all confidences are above low threshold, select an entry which has never been used for prefetch
 * generation (round robin). Layer 3: If the above two fail, randomly evict an entry. Input: rt: incoming (i.e., newly
 * discovered) relationship.
 */
void GRETCH::rt_replacement(RTEntry& rt)
{
    if (rt_table.size() < rt_size) {
        rt_table.push_back(rt);
        rt_table.at(rt_table.size() - 1).inserted_cycle = get_cycle();
        return;
    }

    int index = 0;
    bool found = false;

    for (const RTEntry& rt_entry : rt_table) { // Layer 1
        if (rt_entry.confidence <= -1 * confidence_threshold) {
            found = true;
            break;
        }
        index++;
    }

    if (!found) {
        index = 0;
        for (const RTEntry& rt_entry : rt_table) { // Layer 2
            if (!rt_entry.gen_pref && random.get<bool>()) {
                found = true;
                break;
            }
            index++;
        }
    }

    if (!found)
        index = random.get<int>(0, rt_size - 1); // Layer 3

    RTEntry& evicted_rt_entry = rt_table.at(index);

    ev_rt_evict(adt_content_train_req,
                evd_producer_pc(evicted_rt_entry.producer),
                evd_consumer_pc(evicted_rt_entry.consumer),
                evd_base_addr(Bitfield(evicted_rt_entry.base_addr).extract_range(52, 0)),
                evd_offset(evicted_rt_entry.offset));

    rt_table.erase(rt_table.begin() + index);
    rt_table.insert(rt_table.begin() + index, rt);
    rt_table.at(index).inserted_cycle = get_cycle();
}

/* Perform redundancy checks before actually inserting a data-dependency relationship into RT.
 * Inputs:
 * index: used to retrieve the data-dependency from temporary RT table
 * comp_id: ADT pair computation ID for this epoch.
 * Note: index, comp_id metadata are not present in real hardware. In the latter, this function is called
 * when an ADT pair computation is done and a data-dependency is found.
 */
void GRETCH::update_rt_raw(int index, int comp_id)
{
    RTEntry rt = rt_table_temp[index];

    // avoid redundancy
    for (RTEntry& rt_entry : rt_table) {
        if ((rt_entry.producer == rt.producer) && (rt_entry.consumer == rt.consumer))
            return; // same producer/consumer

        if (filter_close_base_addrs) {
            uint64_t resident_base_addr = Bitfield(rt_entry.base_addr).extract_range(52, lo_bit);
            uint64_t incoming_base_addr = Bitfield(rt.base_addr).extract_range(52, lo_bit);

            if ((rt_entry.producer == rt.producer) && (resident_base_addr == incoming_base_addr)
                && (rt_entry.offset == rt.offset))
            {
                if (rt.base_addr < rt_entry.base_addr) {
                    ev_rt_evict(adt_content_train_req,
                                evd_producer_pc(rt_entry.producer),
                                evd_consumer_pc(rt_entry.consumer),
                                evd_base_addr(Bitfield(rt_entry.base_addr).extract_range(52, 0)),
                                evd_offset(rt_entry.offset));

                    rt_entry.consumer = rt.consumer;
                    rt_entry.base_addr = rt.base_addr;
                    rt_entry.confidence = 0;
                    rt_entry.valid_prediction = false;
                    rt_entry.inserted_cycle = get_cycle();

                    ev_rt_insert(adt_content_train_req,
                                 evd_producer_pc(rt.producer),
                                 evd_consumer_pc(rt.consumer),
                                 evd_base_addr(rt.base_addr),
                                 evd_offset(rt.offset),
                                 evd_comp_id(comp_id));
                }
                return; // same producer and close base addr: to prevent duplicate-ish relation from clogging the RT
                        // and duplicate-ish prefetch candidates from being generated
            }
        }
    }

    rt_replacement(rt);

    ev_rt_insert(adt_content_train_req,
                 evd_producer_pc(rt.producer),
                 evd_consumer_pc(rt.consumer),
                 evd_base_addr(rt.base_addr),
                 evd_offset(rt.offset),
                 evd_comp_id(comp_id));

    pmu_event(adt_content_train_req, pmu_num_data(archx::PMU::L1_PREFETCH_GR_RT_INSERT));
}

/* Update RT when an ADT pair computation is done.
 * Inputs:
 * index: used to retrieve the data-dependency from temporary RT table (index = -1 indicates no data-dependency has been
 * found in this computation) comp_id: ADT pair computation ID for this epoch. Note: index, comp_id metadata are not
 * present in real hardware. In the latter, this function is simply called when an ADT pair computation is done.
 */
void GRETCH::update_rt(int index, int comp_id)
{
    num_comp_remaining--; // For this epoch, number of ADT pair computations left to be handled.

    if (state != GretchState::TRAIN)
        return;

    AMPERE_ASSERT(!inactive(),
                  "RT should not be updated when Gretch training is gated and when privilege level is higher than EL0");

    if (index >= 0) { // data-dependency relationship came out of this computation; need to update RT
        rt_table_temp[index].el = current_el_gpc_stage;
        update_rt_raw(index, comp_id);
    }

    // all ADT pair computations for this epoch are done
    if (num_comp_remaining == 0) {
        ev_comp_queue_handled();
        state = GretchState::WAIT_TRIGGER;
    }
}

// only for PV purposes. Called every time LSU calls run().
// Force-insert a given relationship into RT.
// The purpose is to reproduce/replay what RTL does.
void GRETCH::force_inject_in_rt()
{
    if (force_insert && !force_inserted) {
        if ((time_of_first_commit != 0) && ((get_cycle() - time_of_first_commit) >= force_cycle)) {
            RTEntry rt_entry = RTEntry(0xea_pc, 0x17e_pc, (0x429626f2c_va).value(), 2);
            rt_replacement(rt_entry);
            ev_rt_insert(adt_content_train_req,
                         evd_producer_pc(0xea_pc),
                         evd_consumer_pc(0x17e_pc),
                         evd_base_addr((0x429626f2c_va).value()),
                         evd_offset(2));
            pmu_event(adt_content_train_req, pmu_num_data(archx::PMU::L1_PREFETCH_GR_RT_INSERT));
            force_inserted = true;
            return;
        }
    }
}

/* Increase/decrease the confidence of a specific data-dependency relationship saved in RT.
 * Inputs:
 * rt_entry: RT entry whose confidence should be updated.
 * inc: increment confidence if inc is true.
 */
void GRETCH::update_confidence(RTEntry& rt_entry, bool inc)
{
    // If confidence is dynamic, we stop confidence tracking once we reach MIN threshold
    // If confidence is not dynamic, we stop confidence tracking once we reach MAX/MIN threshold
    AMPERE_ASSERT_ERROR(
        !dynamic_conf || !(rt_entry.confidence <= -1 * confidence_threshold),
        "Confidence measurement mode is dynamic. Already at MIN confidence but confidence tracking did not stop.");

    AMPERE_ASSERT_ERROR(
        dynamic_conf
            || !((rt_entry.confidence >= confidence_threshold) || (rt_entry.confidence <= -1 * confidence_threshold)),
        "Confidence measurement mode is non-dynamic. Already at MIN/MAX confidence but confidence "
        "tracking did not stop.");

    if (inc) {
        rt_entry.confidence++;

        // In dynamic confidence mode, confidence can only reach up to threshold + 1
        // Background information: RTL has 3 bits to represent confidence, values ranging from
        // 0 to 7. Prefetch generation allowed for confidence = 6, 7. In Panthera,
        // values of 6 and 7 are equivalent to 3 and 4.
        if (dynamic_conf && (rt_entry.confidence >= confidence_threshold + 1))
            rt_entry.confidence = confidence_threshold + 1;
    } else
        rt_entry.confidence--;
}

/* Develop confidence on RT entries at commit time.
 * If the incoming load's PC matches a producer PC in RT, predict an address and save it. This address is NOT a prefetch
 * candidate. If the incoming load's PC matches a consumer PC in RT, match the incoming vaddr aginst the saved predicted
 * address to update confidence. Inputs: u: provides <PC, virt_addr> of the incoming load. data_val: Data value of the
 * incoming load queried at commit time. data_valid: data is valid for making prediction. Included because, when data is
 * not found, data_val = 0 sent and used wrongly.
 */
void GRETCH::update_confidence_at_commit(BackendMemReq req, uint64_t data_val, bool data_valid)
{
    auto pc = ixu_pc(req.pc);
    auto vaddr = req.virt_addr;

    for (RTEntry& rt_entry : rt_table) {
        if ((rt_entry.consumer == pc) && rt_entry.valid_prediction) {
            if (rt_entry.prediction_cycle == get_cycle()) {
                // cannot generate prediction and match it in the same cycle
                ev_same_cycle_conf_measure_dropped(req);
            } else {
                update_confidence(rt_entry, rt_entry.predicted_addr == vaddr);
            }
            rt_entry.valid_prediction = false;
        } else if (data_valid && (rt_entry.producer == pc)) {
            // If confidence is dynamic, we stop confidence tracking once we reach MIN threshold
            // If confidence is not dynamic, we stop confidence tracking once we reach MAX/MIN threshold
            bool confidence_cond = (dynamic_conf) ? (rt_entry.confidence > -1 * confidence_threshold)
                                                  : ((rt_entry.confidence < confidence_threshold)
                                                     && (rt_entry.confidence > -1 * confidence_threshold));

            if (confidence_cond) {
                AMPERE_ASSERT(!inactive(),
                              "Gretch should not generate predicted address for confidence measurement when training "
                              "is gated or when context is non-EL0");

                if (!((data_val * rt_entry.offset) >= (std::numeric_limits<uint64_t>::max() - rt_entry.base_addr))) {
                    rt_entry.predicted_addr = VirtAddr(rt_entry.base_addr + data_val * rt_entry.offset);
                    rt_entry.valid_prediction = true;
                    rt_entry.prediction_cycle = get_cycle();
                }
            }
        }
    }
}

/* Compute prefetch candidate address and stage the prefetch candidate in the prefetch queue.
 * Inputs:
 * rt_entry: data dependency relationship on which the prefetch candidate address should be calculated.
 * data: Data value of the producer used to generate prefetch candidate address.
 * req: req of the producer (either PCP's trigger uop or uop of the demand refill)
 */
void GRETCH::compute_prefetch_candidate(RTEntry& rt_entry, uint64_t data, BackendMemReq req)
{
    // first check for overflow
    if ((data * rt_entry.offset) >= (std::numeric_limits<uint64_t>::max() - rt_entry.base_addr))
        return;

    auto pred_addr = VirtAddr(rt_entry.base_addr + data * rt_entry.offset); // array-indirect
    if (max_address_range && pred_addr.extract_upper_range(max_address_range) != 0ULL)
        return;

    auto page_number = pred_addr.extract_range(max_address_range ? max_address_range - 1 : 63,
                                               PageSize(PageSize::Size::PS_4KB).page_bits());
    if (page_number == 0ULL && drop_nullptr)
        return;

    BackendMemReq pf(pred_addr, {}, req, this->prefetch_type());
    if (!prefetch_request->can_put()) {
        if (pq_drop_incoming)
            ev_pq_dropped_full(req, VirtAddr(pred_addr.extract_range(52, 0)));

        overflow(pf);
        return;
    }

    if (req.uop)
        req.uop->set_generated_L1_prefetch();

    pf.consumer_pc = rt_entry.consumer;
    if (secure_gretch)
        pf.exception_level = archx::EL0;
    prefetch_request->put(pf);
    ev_pq_insert(req, VirtAddr(pred_addr.extract_range(52, 0)));
}

// Generate prefetches using RT entries.
void GRETCH::generate_prefetches(CLDataSlice& cl_data_slice)
{
    const auto& req = cl_data_slice.req;
    uint64_t data = cl_data_slice.data;
    bool last_data_slice = cl_data_slice.last_data_slice;
    auto pc = ixu_pc(req.pc);

    AMPERE_ASSERT_ERROR(!last_data_slice || !cacheline_buffers.empty(),
                        "Cacheline buffer emptied before last data slice is used for prefetch generation");

    for (RTEntry& rt_entry : current_cacheline_rt_hit_entries) {
        if ((rt_entry.producer != pc) || (rt_entry.confidence < confidence_threshold))
            AMPERE_ERROR("RT entries staged aside for cacheline stepping are not valid");

        if (pref_this_cycle < data_bw) {
            compute_prefetch_candidate(rt_entry, data, req);

            // just for debug: if it's a unique relationship being used to generate prefetches
            // for the first time, save it away for dumping at the end of execution.
            if (std::find(dd_pref_cand.begin(), dd_pref_cand.end(), rt_entry) == dd_pref_cand.end()) {
                rt_entry.prefetch_gen_cycle = get_cycle();
                dd_pref_cand.push_back(rt_entry);
            }

            pref_this_cycle++;
        }
    }

    // if this is the last data slice, we are now finally done processing every data slice in the producer cacheline.
    if (last_data_slice) {
        cacheline_buffers.pop_front();
        current_index_in_cacheline = 0; // reset index
        current_cacheline_num_rt_hits = 0;
        current_cacheline_rt_hit_entries.clear();
        AMPERE_ASSERT_ERROR(
            supplied_cl_data.empty(),
            "The intermediate data supply storage should be empty when we discard the cacheline from the CL buffer.");
    }
}

uint8_t GRETCH::hit_producer_count(archx::ProgramCounter pc, bool set_aside_entries)
{
    AMPERE_ASSERT_ERROR(!set_aside_entries || current_cacheline_rt_hit_entries.empty(),
                        "Two cacheline steppings cannot overlap in Gretch");

    auto pcix = ixu_pc(pc);

    uint8_t hits = 0;
    int index = -1;
    std::vector<int> indices;
    for (RTEntry& rt_entry : rt_table) {
        index++;
        if ((rt_entry.producer == pcix) && (rt_entry.confidence >= confidence_threshold)) {
            hits++;

            if (set_aside_entries) {
                indices.push_back(index);
                rt_entry.gen_pref = true;
                current_cacheline_rt_hit_entries.push_back(rt_entry);
            }
        }

        if (hits == 2) // we only support 2 hit RT entries; more than that would be very messy
            break;
    }

    // filter close base addr
    if (set_aside_entries && filter_close_base_addrs_pref_gen && (hits == 2)) {
        uint64_t base_addr0 = current_cacheline_rt_hit_entries[0].base_addr;
        uint64_t base_addr1 = current_cacheline_rt_hit_entries[1].base_addr;

        uint64_t base_addr0_hi = Bitfield(base_addr0).extract_range(52, lo_bit);
        uint64_t base_addr1_hi = Bitfield(base_addr1).extract_range(52, lo_bit);

        uint64_t offset0 = current_cacheline_rt_hit_entries[0].offset;
        uint64_t offset1 = current_cacheline_rt_hit_entries[1].offset;

        if ((base_addr0_hi == base_addr1_hi) && (offset0 == offset1)) {
            if (base_addr0 < base_addr1) {
                current_cacheline_rt_hit_entries.pop_back(); // remove item 1
                rt_table[indices[1]].gen_pref = false;
            } else {
                current_cacheline_rt_hit_entries.erase(current_cacheline_rt_hit_entries.begin()); // remove item 0
                rt_table[indices[0]].gen_pref = false;
            }
            hits = 1;
            AMPERE_ASSERT(current_cacheline_rt_hit_entries.size() == 1,
                          "2-RT-hit did not reduce to single hit despite close base addrs.");
        }
    }

    // events
    if (set_aside_entries && (hits > 0)) {
        if (hits == 1)
            ev_one_hit();
        else {
            ev_two_hit();
            uint64_t base_addr0 = current_cacheline_rt_hit_entries[0].base_addr;
            uint64_t base_addr1 = current_cacheline_rt_hit_entries[1].base_addr;
            uint64_t base_addr0_hi = Bitfield(base_addr0).extract_range(52, lo_bit);
            uint64_t base_addr1_hi = Bitfield(base_addr1).extract_range(52, lo_bit);
            uint64_t offset0 = current_cacheline_rt_hit_entries[0].offset;
            uint64_t offset1 = current_cacheline_rt_hit_entries[1].offset;

            if ((base_addr0_hi == base_addr1_hi) && (offset0 == offset1))
                ev_close_base_addr();
        }
    }

    return hits;
}

/* Inputs:
 * req: trigger req
 * paddr: physical address
 * size: data granularity
 * demand_refill: whether this cacheline is coming from demand refill or pcp refill
 */
void GRETCH::buffer_incoming_fill(BackendMemReq req, PhysAddr phys_addr, unsigned size, bool demand_refill)
{
    if (accepted_pcp_cacheline == get_cycle())
        return; // can't accept more than one incoming PCP cacheline per cycle

    if (cacheline_buffers.size() == cacheline_buffers.capacity())
        return; // can't accept cacheline because cacheline buffers are full

    uint8_t num_rt_hits = hit_producer_count(req.pc, false);

    if (num_rt_hits == 0)
        return; // no need to waste cacheline buffer and handling logic if the pc does not exist as a high confidence
                // entry in RT

    // current_el_gpc_stage is registered in GPC stage when Gretch sees a committing load.
    // Cacheline staging happens at load complete stage.
    // By the time Gretch registers an EL change in the GPC stage, an EL1 uop may be in the load complete stage and
    // provide a fill. To prevent this, we use this EL query technique to stop buffering.
    if (!meets_security_policy(req))
        return;

    /* If it is a demand refill, use the items of cacheline starting from X items away from currently "demanded" item (X
     * is a parameter). Instead of scanning the entire cacheline, we do the above to prevent late prefetches from demand
     * refills. */
    auto start_pa = demand_refill ? phys_addr + demand_producer_runahead * size : phys_addr.line_address();
    auto end_pa =
        phys_addr.next_line_address(); // always stop at end of cacheline for both PCP refill and demand refill

    // The above start/end computations may sometimes cause end_pa to be smaller than start_pa (due to the "runahead")
    if (start_pa < end_pa) {
        CacheLine line;
        for (auto pa = start_pa; pa < end_pa; pa += size) {
            uint64_t data_val = req.uop->ainst()->state().read(pa, size);
            DBG_GRCH("RD1 {:016x}", data_val);
            line.push_back(data_val);
        }

        cacheline_buffers.push_back(std::make_pair(req, line));
        accepted_pcp_cacheline = get_cycle();
    } else {
        ev_skipped_producer_fill(req, start_pa);
    }
}

void GRETCH::process_and_supply_cacheline_data()
{
    AMPERE_ASSERT_ERROR(!cacheline_buffers.empty(),
                        "Gretch is attempting to process cacheline data on empty cacheline buffer");

    auto [current_cacheline_req, line] = cacheline_buffers.front();

    if (current_index_in_cacheline == line.size())
        return; // entire cacheline is already processed; it is not yet removed because we are waiting for the last item
                // to compute prefetch address

    // as we start stepping through the cacheline, determine the number of RT hit entries and set aside those entries.
    if (current_index_in_cacheline == 0) {
        current_cacheline_num_rt_hits = hit_producer_count(current_cacheline_req.pc, true);

        for (const auto& rt_entry : current_cacheline_rt_hit_entries) {
            ev_producer_fill(current_cacheline_req,
                             evd_producer_pc(rt_entry.producer),
                             evd_consumer_pc(rt_entry.consumer),
                             evd_base_addr(Bitfield(rt_entry.base_addr).extract_range(52, 0)),
                             evd_offset(rt_entry.offset));
            pmu_event(current_cacheline_req, pmu_num_data(archx::PMU::L1_PREFETCH_GR_PRODUCER_FILL));

            // Cross-over (e.g., a fill from EL1 is using an RT entry established in EL0) happens due to 9-bit PC
            // aliasing. The events below ignore cross-over since we are interested in benefit to EL0 and EL1
            if ((current_el_gpc_stage == archx::EL0) && (rt_entry.el == archx::EL0))
                ev_producer_fill_el0(current_cacheline_req);
            else if ((current_el_gpc_stage == archx::EL1) && (rt_entry.el == archx::EL1))
                ev_producer_fill_el1(current_cacheline_req);
        }
    }

    uint64_t data = line[current_index_in_cacheline];

    current_index_in_cacheline++; // increment index for next data item inspection
    bool last_data_slice = false;
    if (current_index_in_cacheline == line.size()) {
        last_data_slice = true;
        // Do not yet remove cacheline and make space in the buffers;
        // wait until the last data slice has been used to compute prefetch address.
    }

    AMPERE_ASSERT_ERROR(supplied_cl_data.size() < supplied_cl_data.capacity(),
                        "Cacheline data slices should not be lost in Panthera-only Gretch storage called "
                        "supplied_cl_data. Increase the number of entries in this storage.");

    supplied_cl_data.push_back(CLDataSlice(current_cacheline_req, data, last_data_slice));
}

/* Called at commit time to check whether data can be/should be grabbed for the committing load.
 * Input:
 * pc: PC of the committing load (it is actually IXU_PC, i.e., truncated PC available to LSU)
 * alloc_id/lob_id: used to check whether we can retrieve data
 * Output:
 * return true if committing load's data is available.
 */
bool GRETCH::find_data_at_commit(archx::ProgramCounter pc, uint64_t alloc_id, uint64_t lob_id)
{
    if (use_drt && manage_drt(DRTOpType::LOOKUP_DATA, alloc_id, lob_id))
        return true; // DRT already allocates LOB id by checking against pth; no need to check pth here

    if (!use_drt && !inactive()) {
        // If gated or higher-than-EL0 exception level -> a perfect data retrieval technique should not provide data
        for (auto it : pth) {
            if (it == pc) // perfect data retrieval from LOBEntry; just check if pc belongs to pth
                return true;
        }
    }

    return false;
}

/* Called after PCP train to collect a PC which has exhibited the desired PCP state change.
 * Check against already-existing PCP-feedback PCs and include the incoming PC if it does not already exist.
 * Input:
 * pc: PC that has exhibited our desired PCP state change
 * Output:
 *  return true if the pc is actually inserted in PTH
 */
bool GRETCH::update_pth(archx::ProgramCounter pc)
{
    auto pcix = ixu_pc(pc);
    for (const auto& it : pth) {
        if (it == pcix)
            return false;
    }

    pth.push_back(pcix);
    return true;
}

/* For a commiting load, do the following:
 * 1) Lookup data in DRT and invalidate it.
 * 2) Direct the load's PC, addr (and data, if available) to ADT and RT.
 * Input:
 * req: Uop corresponding to the committing load.
 * use_for_gretch_training: All committed loads will access DRT for invalidation purposes. But only first few used for
 * gretch training (i.e., for ADT and RT).
 */
std::pair<uint64_t, bool> GRETCH::handle_load_committed_from_lsu(BackendMemReq req, bool use_for_gretch_training)
{
    // extract the EL id of the committing load uop
    current_el_gpc_stage = *req.exception_level;

    auto pc = ixu_pc(req.pc);
    uint64_t size = std::min(8U, req.data_size);
    uint64_t data_val = 0; // see below -> only provide data val if it is found in DRT

    bool activated = false;
    bool data_found = find_data_at_commit(
        pc, req.alloc_id, req.memory_buffer_id); // lookup data in DRT; invalidate DRT entry if data is found

    if (req.uop->inum().id() == 1)
        time_of_first_commit = get_cycle();

    if (use_for_gretch_training) {
        // send to ADT and RT
        if (data_found) {
            data_val = req.uop->ainst()->state().read(*req.phys_addr, size);
            DBG_GRCH("RD2 {:016x}", data_val);
            activated = activate_adt_populate_at_commit(req, data_val);
            ev_drt_data_read(req);
        }

        if (!activated)
            populate_adt_at_commit(
                req, data_val, data_found); // if activated=true; this function has been already called

        return {data_val, data_found};
    }

    return {0 /*data val*/, false /* data found*/}; // This committed load is not used for training,
                                                    // so this information does not get used anyways.
}

// partial PC actually available to LSU
archx::ProgramCounter GRETCH::ixu_pc(archx::ProgramCounter pc)
{
    return archx::ProgramCounter(pc.extract_range((unsigned)10, (unsigned)2));
}

// Returns true if gretch wants to run in the next cycle
bool GRETCH::work_to_do() const
{
    return !adt_table.empty() || !pcp_training.empty() || !comp_queue.empty() || !cacheline_buffers.empty()
           || !supplied_cl_data.empty() || !current_cacheline_rt_hit_entries.empty();
}

/* One-stop function to perform all sorts of operation on Gretch's data storage for commit-time data retrieval.
 * Inputs:
 * op: type of operation.
 * alloc_id/lobId: required for all types of op on this data storage.
 * pc: only required for LOBID allocation -> allocate LOB id only for a PC that exists in PTH.
 * uop: added later just for event purposes
 * Output:
 * return true if the operation in question is performed successfully (e.g., we found data)
 */
bool GRETCH::manage_drt(
    DRTOpType op, uint64_t alloc_id, uint64_t lobId, std::optional<ProgramCounter> pc, const Uop& uop)
{
    switch (op) {
    case DRTOpType::ALLOCATE_LOB_ID: {
        if (!pc)
            AMPERE_ERROR("LOB ID allocation in DRT requires an input PC to check against PTH");
        archx::ProgramCounter pcix = ixu_pc(*pc);
        bool in_pth = (std::find(pth.begin(), pth.end(), pcix) != pth.end());
        bool adt_producer = (state == GretchState::POPULATE_ADT) && (adt_table[0].pc == pcix);
        bool conf_track_in_progress = false;

        for (const RTEntry& rt_entry : rt_table) {
            bool pc_hit = rt_entry.producer == pcix;
            if (dynamic_conf) // track confidence as long as confidence is above MIN threshold
                conf_track_in_progress = pc_hit && (rt_entry.confidence > -1 * confidence_threshold);
            else // track confidence as long as confidence is above MIN threshold and below MAX threshold
                conf_track_in_progress = pc_hit && (rt_entry.confidence < confidence_threshold)
                                         && (rt_entry.confidence > -1 * confidence_threshold);
        }

        if (in_pth || adt_producer || conf_track_in_progress)
            return allocate_drt(alloc_id, lobId, uop);
    } break;

    case DRTOpType::WRITE_DATA:
        return write_data_drt(alloc_id, lobId);

    case DRTOpType::INVALIDATE:
        return invalidate_drt(alloc_id, lobId);

    case DRTOpType::LOOKUP_DATA:
        return lookup_data_in_drt(alloc_id, lobId);

    default:
        AMPERE_ERROR("Invalid op on data storage");
    }

    return false;
}

unsigned GRETCH::hash_lob_id(uint64_t lob_id) const
{
    return lob_id % drt_size;
}

// allocate lobId at issue time in DRT unless training is gated or context is non-EL0
bool GRETCH::allocate_drt(uint64_t alloc_id, uint64_t lob_id, const Uop& uop)
{
    if (inactive())
        return false;

    auto index = hash_lob_id(lob_id);
    auto& drt_entry = drt[index];
    if (drt_entry.valid)
        return false; // drop if entry is not invalidated (i.e., not available)

    drt_entry.valid = true;
    drt_entry.lobid = lob_id;
    drt_entry.allocid = alloc_id;
    if (uop) {
        archx::ProgramCounter pcix = ixu_pc(uop->pc());
        auto inum = uop->inum();
        ev_drt_alloc(uop, evd_lobid(lob_id), pcix, evd_drt_alloc_inum(inum.id()));
    } else
        ev_drt_alloc(evd_lobid(lob_id));

    return true;
}

// invalidate a DRT entry unless training is gated or context is non-EL0
bool GRETCH::invalidate_drt(uint64_t alloc_id, uint64_t lob_id)
{
    if (inactive())
        return false;

    auto index = hash_lob_id(lob_id);
    auto& drt_entry = drt[index];
    if (drt_entry.valid && (drt_entry.lobid == lob_id)) { // multiple lob ids can map to same index
        drt_entry.valid = false;
        drt_entry.lobid = std::numeric_limits<uint64_t>::max();
        drt_entry.allocid = std::numeric_limits<uint64_t>::max();
        return true;
    }
    return false;
}

// find data in DRT to populate ADT and for confidence measurement. If data found, invalidate the entry.
// inactive when training is gated or context is non-EL0
bool GRETCH::lookup_data_in_drt(uint64_t alloc_id, uint64_t lob_id)
{
    if (inactive())
        return false;

    bool data_found = false;
    auto index = hash_lob_id(lob_id);
    auto& drt_entry = drt[index];
    if (drt_entry.valid && (drt_entry.lobid == lob_id)) { // lob ids can map to same index
        data_found = true;
        manage_drt(DRTOpType::INVALIDATE, alloc_id, lob_id); // this will invalidate the entry
    }

    return data_found;
}

// write data in DRT at load_complete stage unless training is gated or context is non-EL0.
bool GRETCH::write_data_drt(uint64_t alloc_id, uint64_t lob_id)
{
    if (inactive())
        return false;

    auto index = hash_lob_id(lob_id);
    auto& drt_entry = drt[index];
    if (drt_entry.valid && (drt_entry.lobid == lob_id)) { // multiple lob ids can map to same index
        ev_drt_data_write();
        return true;
    }
    return false;
}

void GRETCH::gate_gretch_train()
{
    // Gating Gretch means shutting down DRT and ADT activities. Basically, Gretch training is shut off.
    // PTH is kept running to speed up DRT population upon wakeup.

    // invalidate all DRT entries so that space is available upon wake-up.
    for (auto& drt_entry : drt) {
        drt_entry.valid = false;
        drt_entry.lobid = std::numeric_limits<uint64_t>::max();
        drt_entry.allocid = std::numeric_limits<uint64_t>::max();
    }

    // Force ADT's state to be wait for trigger. This stops ADT population or training.
    // And, since DRT is gated too, nothing will qualify to "trigger" the ADT.
    state = GretchState::WAIT_TRIGGER;
    num_addr_observed = 0;

    if (!comp_queue.empty()) {
        comp_queue.clear();
        num_comp_remaining = 0;
    }

    gated = true;
}

void GRETCH::ungate_gretch_train()
{
    gated = false;
}

void GRETCH::manage_dynamic_training()
{
    cycles_elapsed_epoch += get_cycle() - previous_cycle;
    previous_cycle = get_cycle();

    if (gated && (cycles_elapsed_epoch >= off_cycles)) {
        cycles_elapsed_epoch = 0; // reset for on period
        ungate_gretch_train();
    } else if (!gated && (cycles_elapsed_epoch >= on_cycles)) {
        cycles_elapsed_epoch = 0; // reset for off period
        gate_gretch_train();
    }
}

void GRETCH::print_state(std::ostream& os) const
{
    fmt::print(os, "gretch state = {}\n", state);
}

void GRETCH::adt_print(std::ostream& os) const
{
    print_state(os);
    fmt::print(os, "number of addresses = {}; threshold = {}\n", num_addr_observed, commit_threshold);
    fmt::print(os,
               "\n| {1:>{0}} | {2:>{0}} | {3:>{0}} | {4:>{0}} | {5:>{0}} | {6:>{0}} | {7:>{0}} | {8:>{0}} | {9:>{0}} | "
               "{10:>{0}} | {11:>{0}}\n",
               10,
               "pc",
               "address0",
               "data0",
               "address1",
               "data1",
               "num_addresses",
               "num_val",
               "lobid0",
               "lobid1",
               "inum0",
               "inum1");

    for (const ADTEntry& adt_entry : adt_table) {
        fmt::print(os,
                   "| {1:>{0}} | {2:>{0}} | {3:>{0}} | {4:>{0}} | {5:>{0}} | {6:>{0}} | {7:>{0}} | {8:>{0}} | {9:>{0}} "
                   "| {10:>{0}} | {11:>{0}} \n",
                   10,
                   adt_entry.pc,
                   adt_entry.addr[0],
                   adt_entry.num_data < 1 ? "N/A" : std::to_string(adt_entry.data[0]),
                   adt_entry.addr[1],
                   adt_entry.num_data < 2 ? "N/A" : std::to_string(adt_entry.data[1]),
                   adt_entry.num_addresses,
                   adt_entry.num_data,
                   adt_entry.lobid[0],
                   adt_entry.lobid[1],
                   adt_entry.inum[0].id(),
                   adt_entry.inum[1].id());
    }

    fmt::print(os, "\n");
}

void GRETCH::comp_queue_print(std::ostream& os) const
{
    fmt::print(os, "= GRETCH COMP QUEUE ==================================================\n");
    fmt::print(os, "\n| {1:>{0}} | {2:>{0}} | {3:>{0}} \n", 8, "ID", "high latency", "Temp RT table index");

    for (const CompInfo comp_info : comp_queue) {
        fmt::print(os,
                   "| {1:>{0}} | {2:>{0}} | {3:>{0}}\n",
                   8,
                   comp_info.comp_id,
                   comp_info.high_latency ? "yes" : "no",
                   comp_info.rt_table_temp_index);
    }

    fmt::print(os, "\n");
}

void GRETCH::rt_print(std::ostream& os) const
{
    fmt::print(os, "= GRETCH RT ==================================================\n");
    fmt::print(os,
               "| {1:>{0}} | {2:>{0}} | {3:>{0}} | {4:>{0}} | {5:>{0}} | {6:>{0}} | {7:>{0}} | {8:>{0}}\n",
               10,
               "Producer",
               "Consumer",
               "Base addr",
               "Offset",
               "predicted Addr",
               "confidence",
               "valid_prediction",
               "used_for_pref_gen");

    for (const RTEntry& rt_entry : rt_table) {
        fmt::print(os,
                   "| {1:>{0}} | {2:>{0}} | {3:>{0}} | {4:>{0}} | {5:>{0}} | {6:>{0}} | {7:>{0}} | {8:>{0}}\n",
                   10,
                   rt_entry.producer,
                   rt_entry.consumer,
                   PhysAddr(rt_entry.base_addr),
                   PhysAddr(rt_entry.offset) /*convert to PhysAddr just to achieve hex printing*/,
                   rt_entry.predicted_addr,
                   rt_entry.confidence,
                   rt_entry.valid_prediction,
                   rt_entry.gen_pref);
    }

    fmt::print(os, "\n");
}

void GRETCH::prefetch_dd_print(std::ostream& os) const
{
    fmt::print(os, "= DD's (a.k.a. RT entries) which led to prefetch candidates ==============================\n");
    fmt::print(os,
               "| {1:>{0}} | {2:>{0}} | {3:>{0}} | {4:>{0}} | {5:>{0}} | {6:>{0}} | {7:>{0}} | {8:>{0}}\n",
               12,
               "Producer",
               "Consumer",
               "Base addr",
               "Offset",
               "predicted Addr",
               "confidence",
               "inserted_cycle",
               "pref_gen_cycle");

    for (const RTEntry& rt_entry : dd_pref_cand) {
        fmt::print(os,
                   "| {1:>{0}} | {2:>{0}} | {3:>{0}} | {4:>{0}} | {5:>{0}} | {6:>{0}} | {7:>{0}} | {8:>{0}}\n",
                   12,
                   rt_entry.producer,
                   rt_entry.consumer,
                   PhysAddr(rt_entry.base_addr),
                   PhysAddr(rt_entry.offset) /*convert to PhysAddr just to achieve hex printing*/,
                   rt_entry.predicted_addr,
                   rt_entry.confidence,
                   rt_entry.inserted_cycle,
                   rt_entry.prefetch_gen_cycle);
    }

    fmt::print(os, "\n");
}

void GRETCH::drt_print(std::ostream& os) const
{
    fmt::print(os, "= DRT ==============================\n");
    fmt::print(os, "| {1:>{0}} | {2:>{0}} | {3:>{0}} | {4:>{0}} \n", 12, "Valid", "Lobid", "Allocid", "Data");

    for (const DRTEntry& drt_entry : drt) {
        fmt::print(os,
                   "| {1:>{0}} | {2:>{0}} | {3:>{0}} | {4:>{0}}\n",
                   12,
                   drt_entry.valid,
                   drt_entry.lobid,
                   drt_entry.allocid,
                   drt_entry.data);
    }

    fmt::print(os, "\n");
}

void GRETCH::pth_print(std::ostream& os) const
{
    fmt::print(os, "= PTH ==============================\n");
    fmt::print(os, "| {1:>{0}} \n", 2, "PC");

    for (const ProgramCounter& pc : pth) {
        fmt::print(os, "| {1:>{0}}\n", 2, pc);
    }

    fmt::print(os, "\n");
}

void GRETCH::cacheline_buffer_print(std::ostream& os) const
{
    fmt::print(os, "= GRETCH CACHELINE BUFFERS ==================================================\n");
    fmt::print(os,
               "size = {}; capacity = {}; current index in cacheline = {}\n",
               cacheline_buffers.size(),
               cacheline_buffers.capacity(),
               current_index_in_cacheline);

    for (const auto& e : cacheline_buffers) {
        fmt::print(os, "pc = {} ", e.first.pc);
        for (auto jt : e.second)
            fmt::print(os, "{} ", jt);
        fmt::print(os, "\n");
    }

    fmt::print(os, "\n");
}

void GRETCH::prefetch_queues_print(std::ostream& os) const
{
    fmt::print(os, "= GRETCH PREFETCH QUEUE (oldest first) ============================================\n");
    fmt::print(os, "| {1:>{0}} \n", 4, "Entry vaddr");

    for (unsigned i = 0, l = prefetch_request->size(); i < l; ++i) {
        fmt::print(os, "| {1:>{0}} \n", 4, prefetch_request->get(i).virt_addr);
    }
    fmt::print(os, "\n");
}

void GRETCH::dump(std::ostream& os) const
{
    fmt::print(os, "= GRETCH ADT ==================================================\n");
    adt_print(os);
    rt_print(os);
    prefetch_dd_print(os);
}

L1P_GRETCH::L1P_GRETCH(const sc_core::sc_module_name& name) : GRETCH(name), pcp(get_nearest_relative<L1P_PCP>(this))
{
    AMPERE_ASSERT(pcp, "PCP required for GRETCH");

    pcp->training.bind(pcp_training);
    get_clock()->add_zdelay_connection(pcp, this);
}

void L1P_GRETCH::launch_impl(BackendMemReq req)
{
    if (aggressive_pcp_control)
        pcp->launch_related(req);
}

} // namespace ampere::core

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
