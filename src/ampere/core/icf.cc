//
// Copyright (c) 2018-2024 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/core/icf.h"
#include "ampere/archx/bitfield.h"
#include "ampere/archx/sysreg.h"
#include "ampere/archx/translation.h"
#include "ampere/core/branch_check.h"
#include "ampere/archx/addr.h"
#include "ampere/archx/pmu.h"
#include "ampere/archx/branch_target.h"
#include "ampere/archx/branch_type.h"
#include "fmt/core.h"

namespace ampere::core {

using namespace uarch_common;

ICF::ICF(const sc_core::sc_module_name& name)
    : Module(name),
      tag_pipes("tag_pipeline",
                {AMPERE_PIPE_STAGE_M(ICF, tag_mux),
                 AMPERE_PIPE_STAGE_M(ICF, tag_array_lookup),
                 AMPERE_PIPE_STAGE_M(ICF, tag_hit_miss),
                 AMPERE_PIPE_STAGE_M(ICF, tag_feq_write)}),
      data_pipes("data_pipeline",
                 {
                     AMPERE_PIPE_STAGE_M(ICF, data_mux),
                     AMPERE_PIPE_STAGE_M(ICF, data_array_access),
                     AMPERE_PIPE_STAGE_M(ICF, data_predecode),
                     // Note: ID04 is intentionally unrepresented in our modeling. In
                     // RTL, ICF sends instructions to the IQ in ID03 and occasionally
                     // "un-sends" them in ID04 based on BTC clears. I have modeled this
                     // to simply never send the instruction in ID03 in the first place
                     // if there would be a BTC clear because the modeling was easier,
                     // which means we have no need to model ID04.
                 },
                 1,
                 nullptr,
                 data_pipeline_comparator)
{
    ifb.l2_indexer.bind(l2_indexer); // hierarchical bind from IFB up to ICF
    itlb.driver.bind(driver_port);
    connect(resume_fetch_after_flush).name("resume_fetch_after_flush_pipe").latency(stall_cycles_after_flush);
}

bool ICF::run()
{
    reclaim_credits();
    accept_tlb_invalidations();
    accept_tlb_miss_acks();
    accept_tlb_miss_returns();
    accept_cache_miss_returns();
    accept_cache_snoops();
    read_ittage_stall();
    can_write_iq = calc_can_write_iq();

    if (flush_port->can_read()) {
        flush();
        can_fetch = false;
        resume_fetch_after_flush.put(true);
    }

    read_fetch_port();

    icf_inst_fifo_full_this_cycle = false;
    wrong_path_this_cycle = false;
    instructions_written_this_cycle = 0;
    if (loop_replay) {
        loop_mode_fetch();
    } else {
        tag_banks_this_cycle.clear();
        data_banks_this_cycle.clear();
        tlb_fill_this_cycle = false;
        tag_pipes.run();
        data_pipes.run();
    }

    write_feq();
    ifb_scheduler();
    pmu_events();

    return true;
}

void ICF::end_of_elaboration()
{
    AMPERE_ASSERT(cache.enable_perfect_mode || !itlb.enable_perfect_mode,
                  "itlb enable_perfect_mode likely does not do what you want! Perfect ITLB always returns physical "
                  "addresses of 0x0, and as a result every icache access after the first will be a hit.");
    AMPERE_ASSERT(icache_bank_bits <= 6,
                  "icache cannot use more than 6 bank bits because we can only use un-translated bits\n");
    AMPERE_ASSERT(
        !split_fetches_across_cycles || data_pipes.width() == 1,
        "The scheduling with two instruction data pipelines requires that we not split fetches across cycles\n");
}

/*
 * Send information to IDR about our TLB and cache misses to help with stall
 * cause attribution and count instruction fetch latency-related PMUs.
 */
void ICF::pmu_events()
{
    if (!feq.empty()) {
        if (enable_per_cycle_pmu) {
            for (auto& fetch : feq) {
                if (!fetch.second->ready)
                    pmu_event(pmu_num_data(archx::PMU::INST_FETCH_PERCYC));
            }
        }

        // Check if oldest FEQ entry waiting for L2 fill
        if (pmu_l2_miss_in_icf->can_read()) {
            auto l2_miss_phys_addr = pmu_l2_miss_in_icf->read();

            for (auto& fetch : feq) {
                if (fetch.second->phys_addr && l2_miss_phys_addr.same_cache_line(*fetch.second->phys_addr))
                    fetch.second->missed_l2icache = true;
            }
        }

        // Oldest FEQ entry is waiting for L2 fill
        if (feq.begin()->second->missed_l2icache)
            pmu_idr_l2_miss->put(true);

        // Oldest FEQ entry is waiting for L1 fill
        if (feq.begin()->second->missed_icache)
            pmu_idr_icache_miss->put(true);

        // Oldest FEQ entry is waiting for translation
        if (feq.begin()->second->missed_itlb)
            pmu_idr_tlb_miss->put(true);

        if (!instructions_written_this_cycle) {
            // Note: This counts in RTL even when we are in loop mode and are
            // intentionally not dispatching instructions!
            pmu_event(pmu_num_data(archx::PMU::ICF_STALL));
            pmu_idr_mem_stall->put(true);
        }
        if (icf_inst_fifo_full_this_cycle)
            pmu_event(pmu_num_data(archx::PMU::ICF_INST_FIFO_FULL));
    } else {
        pmu_idr_feq_empty->put(true);
    }
}

/*
 * Free credits being returned to ICF from the IDR or L2C
 */
void ICF::reclaim_credits()
{
    while (instruction_credit_return->can_take()) {
        auto credit = instruction_credit_return->take();
        release_credit(credit);
    }
    while (miss_request_scheduled->can_take()) {
        auto mem_req = miss_request_scheduled->take();
        auto fbid = mem_req->id;
        ifb.l2c_scheduled_request(fbid);
    }
}

/*
 * Accept incoming fetches and BTB clears produced by the BPU. Clear the pipe
 * if necessitated by a clear, and save the incoming fetches off to be
 * scheduled later.
 */
void ICF::read_fetch_port()
{
    AMPERE_ASSERT(next_fetches.empty(), "next_fetches should be empty at the beginning of a cycle");

    std::optional<FetchId> cleared_fetch_id;
    std::optional<FetchId> post_clear_nextline_target_fetch_id;
    if (bp_clear_port->can_take()) {
        auto clear_fetch = bp_clear_port->take();
        auto clear_type = *clear_fetch->clear_type;
        if (clear_fetch->post_clear_nextline_target_fetch_id) {
            post_clear_nextline_target_fetch_id = clear_fetch->post_clear_nextline_target_fetch_id.value();
            clear_fetch->post_clear_nextline_target_fetch_id.reset();
        }
        cleared_fetch_id = clear_fetch->fetch_id;

        btb_clear_event(clear_fetch, btb_clear_type(clear_type));

        pmu_event(pmu_num_data(archx::PMU::BPU_BTB_CLEAR));
        pmu_idr_btb_clear->put(true);
        auto br_type = clear_fetch->get_prediction().branch_type;
        if (is_conditional(br_type)) {
            pmu_event(pmu_num_data(archx::PMU::BPU_BTB_CLEAR_CONDITIONAL_MISPREDICT));
        } else if (is_unconditional(br_type)) {
            pmu_event(pmu_num_data(archx::PMU::BPU_BTB_CLEAR_UNCONDITIONAL_MISPREDICT));
            if (is_indirect(br_type))
                pmu_event(pmu_num_data(archx::PMU::BPU_BTB_CLEAR_IND_TARGET_MISPREDICT));
        }

        if (clear_fetch->fetch_id <= read_fetch)
            partial_flush_pipe(clear_fetch->fetch_id, clear_fetch->loop_abort);
        between_flush_and_first_clear = false;
    }

    while (fetch_port->can_take()) {
        auto next_fetch = fetch_port->take();
        AMPERE_ASSERT(highest_icf_fetch_id < next_fetch->fetch_id,
                      "ICF attempting to set highest fetch ID to {}, which is lower than current value {}",
                      next_fetch->fetch_id,
                      highest_icf_fetch_id);
        highest_icf_fetch_id = next_fetch->fetch_id;

        // If we are receiving these fetches in the same cycle we receive a
        // frontend clear, we must check to see if they are younger than the
        // clear (and therefore must be flushed). But in some cases, we can
        // inject a known-nextline fetch simultaneously with a clear
        // (post_clear_nextline_target_fetch_id), so we avoid clearing that
        // special fetch if we see it.
        if (cleared_fetch_id && cleared_fetch_id.value() < next_fetch->fetch_id
            && (!post_clear_nextline_target_fetch_id
                || post_clear_nextline_target_fetch_id.value() != next_fetch->fetch_id))
        {
            fetch_cleared_event(next_fetch, fetch_id_data(next_fetch->fetch_id));
            next_fetch->feq_credit->release();
        } else {
            if (trinity_repipeline && between_flush_and_first_clear) {
                next_fetch->delay_id_pipe = true;
                feq[next_fetch->fetch_id] = next_fetch;
            } else {
                next_fetches.push_back(next_fetch);
            }
            feq_read_event(next_fetch, fetch_id_data(next_fetch->fetch_id));
            read_fetch = next_fetch->fetch_id;
        }
    }
}

/*
 * Corresponds to IT01/IM01
 *
 * Decide which type of request to send down the tag pipeline based on
 * priority. That priority is:
 *  1) cache invalidations (snoops from L2C)
 *  2) cache fills (responses from L2C)
 *  3) TLB invalidations (from MMU via L2C)
 *  4) TLB fills (responses from MMU)
 *  5) new FEQ lookups (cachelines from BPU)
 *
 *  Note: Because the IT (data tag) and IM (TLB tag) pipelines are separate, in
 *  RTL a TLB invalidation can technically go down the IT pipeline
 *  simultaneously with either a data tag fill or data invalidation (snoop). We
 *  do not model separate pipelines because we expect this case to be rare
 *  enough to not appreciably impact performance.
 */
bool ICF::tag_mux(Pipeline<TagOp>::Data data)
{
    while (resume_fetch_after_flush.can_take())
        can_fetch = resume_fetch_after_flush.take();

    // If holdover from the previous cycle, decrement the stall counter and
    // allow this operation to proceed to the next stage if done stalling
    if (data.initialized()) {
        auto [_, unique_bank] = tag_banks_this_cycle.insert(data.get()->bank.value());
        if (!unique_bank)
            return false;
        data.get()->stall_cycles_remaining--;
        return data.get()->stall_cycles_remaining == 0;
    }

    // 1) cache invalidations (snoops from L2C)
    if (!snoop_fifo.empty()) {
        auto snoop = snoop_fifo.front();
        auto unique_bank = get_bank(snoop->pc, tag_banks_this_cycle);
        if (!unique_bank)
            return false;
        data = make<TagOp>(snoop, *unique_bank);
        snoop_fifo.pop_front();
        return true;
    }

    // 2) cache fills (responses from L2C)
    auto oldest_cache_fill = std::find_if(feq.begin(), feq.end(), [](auto p) {
        return p.second->ready && p.second->fills_icache && !p.second->scheduled_it_pipe;
    });
    if (oldest_cache_fill != feq.end()) {
        auto unique_bank = get_bank(oldest_cache_fill->second->program_counter, tag_banks_this_cycle);
        if (!unique_bank)
            return false;
        data = make<TagOp>(oldest_cache_fill->second, *unique_bank, true /*is fill*/);
        oldest_cache_fill->second->scheduled_it_pipe = true;
        icache_lookup_lockout_end_cycle = get_cycle() + Cycle(cache_fill_lookup_stall);
        return true;
    }
    bool lookups_allowed = get_cycle() > icache_lookup_lockout_end_cycle;

    // 3) TLB invalidations (from MMU via L2C)
    if (get_cycle() < itlb_lockout_end_cycle) {
        // We must wait 16 cycles after a TLBI is sent before sending another
        // TLBI, a TLB fill, or a new lookup. Cache snoops and fills are
        // allowed to proceed during this time, however.
        return false;
    } else if (!tlbi_queue.empty()) {
        auto tlbi = tlbi_queue.front();
        data = make<TagOp>(tlbi);
        tlbi_queue.pop_front();
        itlb_lockout_end_cycle = get_cycle() + Cycle(tlbi_stall_latency);
        return true;
    }

    // 4) TLB fills (responses from MMU)
    bool valid_tlb_response =
        !tlb_response_queue.empty() && itlb_misses.find(tlb_response_queue.front()->id) != itlb_misses.end();
    if (lookups_allowed && valid_tlb_response) {
        // Note: If we ever allow more than one outstanding ITLB miss, we'll
        // likely need to modify this logic to ensure we get the arbitration
        // right. This logic assumes we can only ever have one, so we assume
        // that if we have an ITLB miss return, we can always send it down the
        // pipeline.
        auto tlb_response = tlb_response_queue.front();
        auto fetch = itlb_misses.find(tlb_response->id);
        auto unique_bank = get_bank(fetch->second->program_counter, tag_banks_this_cycle);
        if (!unique_bank)
            return false;
        data = make<TagOp>(fetch->second, *unique_bank, tlb_response);
        fetch->second->scheduled_it_pipe = true;
        tlb_response_queue.pop_front();
        itlb_misses.erase(tlb_response->id);
        tlb_fill_this_cycle = true;
        return true;
    }

    // 5) new FEQ lookups (cachelines from BPU)
    //
    // Note: This scheduling calculation is a little more involved than those
    // above because we need to check against several additional stall
    // conditions here

    if (!can_fetch) {
        // Stall new FEQ lookups
        return true;
    }

    auto oldest_unscheduled = std::find_if(feq.begin(), feq.end(), [](auto p) {
        return !p.second->ready && !p.second->missed_itlb && !p.second->pending && !p.second->scheduled_it_pipe;
    });
    bool unscheduled_feq = oldest_unscheduled != feq.end();

    // We stall if a fetch hits upon a cleared IFB entry until its response
    // comes back from the L2C
    bool stall_cleared_ifb_hit = unscheduled_feq && oldest_unscheduled->second->phys_addr
                                 && ifb.is_cleared(*oldest_unscheduled->second->phys_addr)
                                 && !ifb.is_unclearable(*oldest_unscheduled->second->phys_addr);

    // We stall until able to record a younger hit to the set of the stalling
    // FEQ entry
    bool stall_cant_record_younger_hit =
        unscheduled_feq && ifb_younger_hit_stall && oldest_unscheduled->second->phys_addr
        && (*ifb_younger_hit_stall)->fetch_id == oldest_unscheduled->second->fetch_id
        && !ifb.can_record_younger_hit(
            (*ifb_younger_hit_stall)->fetch_id, *(*ifb_younger_hit_stall)->phys_addr, cache.get_num_sets());
    // Clear the IFB too-many-younger-hits stall if no longer valid
    if (ifb_younger_hit_stall && !stall_cant_record_younger_hit)
        ifb_younger_hit_stall.reset();

    // Clear the IFB allocation stall if we can allocate to the IFB now
    ifb_allocation_stall = ifb_allocation_stall && !ifb.can_allocate();

    if (unscheduled_feq && lookups_allowed && unacked_itlb_misses.empty() && itlb_misses.empty() && !tlb_fill_this_cycle
        && !itlb_faulted && !ifb_allocation_stall && !stall_cleared_ifb_hit && !stall_cant_record_younger_hit)
    {
        auto unique_bank = get_bank(oldest_unscheduled->second->program_counter, tag_banks_this_cycle);
        if (!unique_bank)
            return false;
        data = make<TagOp>(oldest_unscheduled->second, *unique_bank);
        oldest_unscheduled->second->scheduled_it_pipe = true;
        return true;
    }

    // If we got here, we failed to schedule anything, so record the reason.
    if (stall_cleared_ifb_hit) {
        ev_ifb_stall();
    } else if (itlb_faulted) {
        itlb_faulted_event();
    }
    // TODO count IFB full or cannot-record-younger-hit stall conditions?!?

    return true;
}

/*
 * Corresponds to IT02/IM02
 *
 * Read the ITLB to get PA, lookup the instruction cache tag array to get
 * hit/miss indication and signal that to the ID pipeline (via the Fetch
 * object)
 */
bool ICF::tag_array_lookup(Pipeline<TagOp>::Data data)
{
    auto op = data.get();

    // First, perform the ITLB lookup or fill
    if (op->type == LOOKUP) {
        bool itlb_hit = tlb_lookup(*op->fetch);
        if (auto tr = TranslationContext{(*op->fetch)->program_counter, Bitfield(driver_port->read_sysreg(TCR_EL1))};
            !tr.canonical())
        {
            itlb_faulted = true;
            tag_clear_and_stall(op);
            return true;
        }
        op->itlb_miss = !itlb_hit;
    } else if (op->type == TLB_FILL) {
        // Cause pipeline stall if this translation faulted
        if ((*op->tlb_fill)->faulted) {
            itlb_faulted = true;
            tag_clear_and_stall(op);
            return true;
        } else {
            op->itlb_miss = false;
            translate_fetch_pc(*op->fetch, (*op->tlb_fill)->phys_addr, (*op->tlb_fill)->page_size);
        }
    }

    // If we now have a physical address (due to TLB hit or fill), do the
    // icache lookup
    if ((op->type == LOOKUP || op->type == TLB_FILL) && !op->itlb_miss) {
        bool icache_hit = cache_lookup(*op->fetch);
        op->icache_miss = !icache_hit;
    }
    // Note: There is nothing to do for CACHE_FILL, TLB_INVALIDATION, or
    // CACHE_SNOOP operations in this stage.

    return true;
}

/*
 * Corresponds to IT03/IM03
 *
 * - If lookup and ITLB miss, send MMU request
 * - If (lookup with ITLB hit or ITLB fill) and icache miss, allocate IFB
 *   (clearing IT pipeline if IFB full)
 * - If (lookup with ITLB hit or ITLB fill) and icache hit, lookup IL0 (its
 *   inclusive). Note: we model this in IT02 because its simpler to model
 *   alongside the L1 access)
 * - If cache or TLB invalidation, write the arrays
 */
bool ICF::tag_hit_miss(Pipeline<TagOp>::Data data)
{
    auto op = data.get();
    if (op->type == LOOKUP && op->itlb_miss) {
        tlb_miss(*op->fetch);
        tag_clear_and_stall(op);
        return true;
    } else if (op->type == LOOKUP || op->type == TLB_FILL) {
        if (op->type == TLB_FILL)
            tlb_fill(*op->fetch, *op->tlb_fill);

        pmu_event(pmu_num_data(archx::PMU::INST_FETCH));
        pmu_event(pmu_num_data(archx::PMU::L1I_CACHE));
        pmu_event(pmu_num_data(archx::PMU::L1I_CACHE_RD));

        if (op->icache_miss) {
            if (!ifb_lookup_allocation(*op->fetch)) {
                tag_clear_and_stall(op);
                return true;
            }
        } else {
            if (!ifb_track_cache_hit(*op->fetch)) {
                tag_clear_and_stall(op);
                return true;
            }
        }
    } else if (op->type == CACHE_FILL) {
        auto fetch = *op->fetch;
        std::uint64_t ifb_id = fetch->txid().value;
        bool maybe_snooped = ifb.was_snooped(ifb_id) || ifb.is_unclearable(*fetch->phys_addr);
        cache_refill(fetch, maybe_snooped);
        if (fetch->victim_cache_hit)
            ifb_victim_cache.deallocate(fetch->fetch_id);
        ifb.deallocate(ifb_id);
        fetch->fills_icache = false;
        if (!l1i_access_pmus_only_once) {
            pmu_event(pmu_num_data(archx::PMU::L1I_CACHE));
            pmu_event(pmu_num_data(archx::PMU::L1I_CACHE_RD));
        }
    } else if (op->type == TLB_INVALIDATION) {
        invalidate_tlb(*op->tlbi);
    } else if (op->type == CACHE_SNOOP) {
        cache_snoop(*op->snoop);
    }
    return true;
}

/*
 * Corresponds to IT04
 *
 * In the model, for now at least, this stage mostly just marks the FEQ as no
 * longer executing on the tag pipeline
 */
bool ICF::tag_feq_write(Pipeline<TagOp>::Data data)
{
    auto op = data.get();
    if (op->fetch) {
        (*op->fetch)->scheduled_it_pipe = false;
    }
    return true;
}

/*
 * Clear the FEQ lookup and all FEQ lookups younger than it from the tag (IT)
 * pipeline. This is used when the tag pipeline clears prior to a stall.
 */
void ICF::tag_clear_and_stall(TagOp& clearing_op)
{
    AMPERE_ASSERT(clearing_op->fetch, "Only tag pipeline operations with fetches can clear");
    tag_clear_and_stall(*clearing_op->fetch);
}
void ICF::tag_clear_and_stall(Fetch& clearing_fetch)
{
    for (size_t i = 0; i < tag_pipes.width(); i++) {
        for (size_t j = 0; j < tag_pipes.size(); j++) {
            auto stage = tag_pipes[i][j];
            if (!stage.initialized())
                continue;
            auto& op = stage.get();
            if (op->fetch && (*op->fetch)->fetch_id >= clearing_fetch->fetch_id) {
                (*op->fetch)->scheduled_it_pipe = false;
                stage.invalidate();
            }
        }
    }
}

/*
 * Corresponds to ID01
 *
 * Schedule the oldest FEQ entry which has not yet begun going down the
 * pipeline
 */
bool ICF::data_mux(Pipeline<Fetch>::Data data)
{
    auto oldest_unscheduled = std::find_if(feq.begin(), feq.end(), [](auto p) { return !p.second->scheduled_id_pipe; });
    // Return if there is no work to do because the FEQ is empty
    if (oldest_unscheduled == feq.end())
        return false;
    auto oldest_fetch = oldest_unscheduled->second;
    // Stall if the oldest fetch is not ready (only for Trinity repipeline -
    // see comment below in `data_array_access`)
    if (oldest_fetch->delay_id_pipe && !oldest_fetch->ready)
        return false;

    oldest_fetch->scheduled_id_pipe = true;
    data = oldest_fetch;

    return true;
}

/*
 * Corresponds to ID02
 *
 * In RTL, this is where we mux between IL1/IL0/IFB/L2C reads. In panthera, we
 * really only need to model stalling if we're doing a fill and the IT pipeline
 * is going to do a snoop (or for pre-Trinity, we stall if the data isn't
 * ready).
 */
bool ICF::data_array_access(Pipeline<Fetch>::Data data)
{
    auto fetch = data.get();

    // Pre-Trinity, the data pipeline was aligned with the tag pipeline. To
    // model this, we start the oldest fetch down the pipeline before we know
    // whether it is ready and stall it here at ID02 until it is ready.
    if (!fetch->delay_id_pipe && !fetch->ready)
        return false;

    // ID02 must stall if we are doing a fill but IT01 hasn't scheduled it yet,
    // because the data fill can't get ahead of the tag fill.
    return !fetch->fills_icache || fetch->scheduled_it_pipe;
}

/*
 * Corresponds to ID03
 *
 * Stall if IQ or PC FIFO are full, or if ITTAGE is still processing this FEQ.
 * Perform initial write into IQ.
 */
bool ICF::data_predecode(Pipeline<Fetch>::Data data)
{
    auto fetch = data.get();

    // This fetch cannot be written into the IQ unless and until it is the
    // oldest!
    if (feq.begin()->first != fetch->fetch_id)
        return false;

    // stall if ITTAGE is still processing this fetch
    if (ittage_stall && fetch->fetch_id > *ittage_stall) {
        return false;
    }

    // ID pipeline must stall if there are not enough credits available in IQ
    // (TODO or PC FIFO)
    if (!can_write_iq) {
        icf_inst_fifo_full_this_cycle = false;
        return false;
    }

    // We can only write if we do not have a bank conflict. Note that IL0 hits
    // and IFB and/or IFB victim cache hits do not count as bank conflicts here
    // since they do not read from the data array.
    if (!fetch->fills_icache && !fetch->il0_hit && !get_bank(fetch->program_counter, data_banks_this_cycle)) {
        return false;
    }

    // Perform BTC checks and actual IQ write
    instructions_written_this_cycle += write_queue(fetch, iq_write_max_instrs - instructions_written_this_cycle);

    return fetch->instructions_fetched() == fetch->num_instructions();
}

void ICF::read_ittage_stall()
{
    if (ittage_stall_port->can_take())
        ittage_stall = ittage_stall_port->take();
    else
        ittage_stall = std::nullopt;
}

void ICF::write_feq()
{
    while (!next_fetches.empty()) {
        auto next_fetch = next_fetches.front();
        feq[next_fetch->fetch_id] = next_fetch;
        next_fetches.pop_front();
    }
}

bool ICF::recently_snooped(PhysAddr addr)
{
    // The 5-cycle window for "recently snooped" is a little conservative, but the collision
    // of an L2 request or fill with a snoop to the same line is extremely rare -- we check
    // recent snoops only to prevent occasional false inclusion errors.
    auto s = snooped_lines.begin();
    while (s != snooped_lines.end()) {
        if (get_cycle() - s->second > 5) {
            s = snooped_lines.erase(s);
            continue;
        }
        if (s->first.same_cache_line(addr))
            return true;
        s++;
    }

    return false;
}

/*
 * Send up to one IFB entry's request to the L2C
 */
void ICF::ifb_scheduler()
{
    auto mem_req = ifb.schedule_request();
    if (mem_req.second) {
        auto l1_contents = Request::L1Contents();
        auto set_lines = cache.set_contents(mem_req.second->phys_addr);
        for (const auto& line : set_lines) {
            // Avoid false inclusion errors by not telling the L2 that we have a line which
            // is just about to be snooped out.
            if (!recently_snooped(line.addr))
                l1_contents.add(line.addr);
        }

        // If there are two outstanding requests to different lines in the same L2 set, it's too hard for
        // hardware to figure out which L2 ways are in the L1, so we just give up tell the L2 not to clear
        // any L1-present bits for this request.
        if (ifb.requesting_other_line_same_l2_set(mem_req.second->phys_addr)) {
            ev_no_clear_l1_bits(mem_req.second);
            l1_contents.no_clear_l1_bits = true;
        }

        miss_request->put(Request::Fill(mem_req.second, l1_contents));
    }

    // Needs to be called each cycle to properly account speculative requests
    // to the L2C
    ifb.age_requests();
}

void ICF::translate_fetch_pc(Fetch& fetch, PhysAddr phys_addr, PageSize page_size)
{
    auto page_base = phys_addr.page_address(page_size).value();
    auto untranslated_bits = fetch->program_counter.extract_range(page_size.page_bits() - 1, 0);
    fetch->phys_addr = PhysAddr{page_base | untranslated_bits};
    fetch->page_size = page_size.value();
}

/*
 * Access the L1 ITLB, returning true for a hit, false for a miss
 */
bool ICF::tlb_lookup(Fetch& fetch)
{
    auto tag = L1TLBTag<ProgramCounter>(fetch->program_counter, driver_port->asid());
    auto tlb_access = itlb.lookup(fetch, tag);

    bool hit = bool(tlb_access);
    if (hit)
        translate_fetch_pc(fetch, tlb_access->phys_addr, tlb_access->page_size);
    return hit;
}

/*
 * Send a translation request to the MMU for an earlier TLB miss
 */
void ICF::tlb_miss(Fetch& fetch)
{
    auto id = fetch->txid().value;
    auto mmu_req = make_child<MMUReq>(fetch, id, fetch->program_counter);
    mmu_request->put(mmu_req);

    unacked_itlb_misses.try_emplace(id, fetch);

    fetch->missed_itlb = true;
}

/*
 * Accept acknowledgements from the MMU for translations spawned by previous L1
 * ITLB misses
 */
void ICF::accept_tlb_miss_acks()
{
    if (mmu_ack->can_take()) {
        auto mmuack = mmu_ack->take();
        auto unacked_it = unacked_itlb_misses.find(mmuack->id);

        // If we didn't find an entry in the unacked misses, assume that the fetch
        // requesting the translation was flushed before it was acknowledged by the
        // MMU and drop it.
        if (unacked_it != unacked_itlb_misses.end()) {
            auto fetch = unacked_it->second;
            if (mmuack->otb_id) {
                // Create an entry in itlb_misses for this translation request,
                // indexed by OTB ID (instead of the fetch's transaction ID)
                itlb_misses.insert(std::pair<std::uint64_t, Fetch>(*mmuack->otb_id, fetch));
                unacked_itlb_misses.erase(mmuack->id); // invalidates `unacked_it`
            } else {
                // If the OTB ID in the MMUAck is empty it means the MMU failed
                // to allocate an OTB for this request (potentially because it
                // was too soon after a flush). In this case, the ICF sends the
                // same request to the MMU again.
                tlb_miss(fetch);
            }
        }
    }
}

/*
 * Accept responses from the MMU for translations spawned by previous L1 ITLB
 * misses
 */
void ICF::accept_tlb_miss_returns()
{
    if (mmu_response->can_take()) {
        auto mmuresp = mmu_response->take();

        // If we received an ITLB response of which we have no record, assume
        // we flushed and ignore it (the MMU does not flush translations, so we
        // may receive translation responses for flushed fetches)
        if (itlb_misses.find(mmuresp->id) != itlb_misses.end())
            tlb_response_queue.push_back(mmuresp);
    }
}

/*
 * Fill the TLB array
 */
void ICF::tlb_fill(Fetch& fetch, MMUResp mmuresp)
{
    if (!mmuresp->faulted) {
        auto page_base = mmuresp->phys_addr.page_address(mmuresp->page_size).value();
        auto untranslated_bits = fetch->program_counter.extract_range(mmuresp->page_size.page_bits() - 1, 0);
        fetch->phys_addr = PhysAddr{page_base | untranslated_bits};
        fetch->page_size = mmuresp->page_size.value();

        auto tag = L1TLBTag<ProgramCounter>(ProgramCounter(mmuresp->virt_addr), mmuresp->asid);
        auto tlbentry = L1TLBEntry(mmuresp->phys_addr, mmuresp->page_size);
        itlb.refill(fetch, tag, tlbentry);
    }

    fetch->missed_itlb = false;
}

/*
 * Invalidate the TLB
 */
void ICF::invalidate_tlb(TLBIReq tlbi)
{
    auto& ainst = tlbi->insn->archx_instruction;

    // For TLBI by VA, invalidate specific entries. For everything else, just clear the whole ITLB.
    // FIXME: this still over-invalidates in many cases
    if (ainst->is_tlbi_by_va())
        itlb.invalidate_by_va(
            ainst->tlbi_by_va_asid(), ainst->tlbi_by_va_ttl(), ProgramCounter(ainst->tlbi_by_va_addr()));
    else
        itlb.clear();
}

/*
 * Accept incoming requests to invalidate the TLB, queuing them for scheduling
 * by the IT/IM pipe.
 */
void ICF::accept_tlb_invalidations()
{
    while (tlbi_request->can_take()) {
        auto tlbi = tlbi_request->take();
        tlbi_queue.push_back(tlbi);
    }
}

/*
 * Look up the L0 and L1 instruction caches, returning true if a hit.
 */
bool ICF::cache_lookup(Fetch& fetch)
{
    auto pa = *fetch->phys_addr;

    CacheResult<CacheLine> hit;
    bool fill_l0 = (!use_l0_candidates_table) || l0_candidates.check_candidate(fetch, pa);

    // Try to lookup the "L0" icache first, then full L1
    if (enable_l0_cache) {
        hit = l0.lookup(fetch, pa, true /*update lru*/, false /*no event suppression*/);
        fetch->il0_hit = bool(hit);
    }
    if (!hit) {
        hit = cache.lookup(fetch,
                           pa,
                           true /*update lru*/,
                           false /*no event suppression*/,
                           false /* not prefetch */,
                           pa.line_address().value());
        if (enable_l0_cache && hit && fill_l0)
            l0.refill(fetch, pa, hit.value());
    }

    if (hit) {
        if (enable_coherency_validation)
            coherency_validator->line_present(pa.line_address(), true /*is instruction*/);

        fetch->ready = true;
    }

    return bool(hit);
}

/*
 * Track a cache hit in the IFB. The IFB must track younger hits to the same
 * set on outstanding IFB entries to ensure they doesn't overwrite a way needed
 * for a younger fill when the IFB entry's data comes back.
 *
 * If the IFB is unable to track this cache hit, returns false (causing a
 * stall); returns true on success.
 */
bool ICF::ifb_track_cache_hit(Fetch& fetch)
{
    // Ensure this hit can be tracked in the IFB
    if (!fake_evict_at_miss) {
        if (ifb.can_record_younger_hit(fetch->fetch_id, *fetch->phys_addr, cache.get_num_sets())) {
            ifb.record_younger_hit(fetch->fetch_id, *fetch->phys_addr, cache.get_num_sets());
        } else {
            ifb_younger_hit_stall = fetch;
            return false;
        }
    }
    return true;
}

/*
 * Called on an icache miss to attempt to initiate a new request to the L2C.
 * Looks up the IFB (and IFB victim cache, if applicable) to see if there is an
 * already an outstanding request for this line.
 *
 * Returns true if either the IFB or victim cache lookup was a hit or if we
 * successfully allocated a new IFB entry. Returns false if we need to stall
 * because the IFB lookup/allocation was unsuccessful (if we hit upon a cleared
 * IFB entry or the IFB was full).
 */
bool ICF::ifb_lookup_allocation(Fetch& fetch)
{
    if (!ifb.can_allocate()) {
        // IFB full - stall until an IFB entry frees up
        ifb_allocation_stall = true;
        return false;
    }

    auto id = fetch->txid().value;
    auto [ifb_hit, uncleared] = ifb.lookup(fetch, *fetch->phys_addr);

    if (ifb_hit) {
        auto cleared = ifb.is_cleared(*fetch->phys_addr);
        if (cleared) {
            AMPERE_ASSERT(!uncleared, "should never be both cleared and uncleared!");
            // If we hit on a cleared IFB, stall fetches until the cleared
            // entry gets its response. That response will be dropped, and then
            // this fetch can re-request the line from L2.
            ev_ifb_hit_cleared(fetch);
            return false;
        } else {
            fetch->ready = ifb.is_complete(*fetch->phys_addr);
            fetch->pending = !fetch->ready;
        }
        if (uncleared) {
            fetch->fills_icache = true;
        }
        fetch->missed_icache = !fetch->ready;
        ifb_hit_event(fetch, ev_cleared(cleared));
    } else {
        // We missed the IFB - lookup IFB victim cache if being used, attempt
        // to allocate a new IFB entry if not using the victim cache or if the
        // victim cache lookup was a miss.
        if (use_ifb_victim_cache && ifb_victim_cache.lookup(fetch)) {
            ifb_victim_cache_hit_event(fetch);
            fetch->victim_cache_hit = true;
            fetch->fills_icache = true;
            fetch->ready = true;
            fetch->missed_icache = false;
            fetch->pending = false;
            ifb.allocate(id,
                         fetch->fetch_id,
                         *fetch->phys_addr,
                         fetch->page_size,
                         false /*not a prefetch*/,
                         true /*allocate as completed IFB entry*/);
        } else {
            fetch->fills_icache = true;
            fetch->ready = false;
            fetch->missed_icache = true;
            fetch->pending = true;
            pmu_event(fetch, pmu_num_data(archx::PMU::L1I_CACHE_LMISS));
            if (icache_misses->can_put()) {
                icache_misses->put(1);
            }
            ifb.allocate(id, fetch->fetch_id, *fetch->phys_addr, fetch->page_size);
        }
    }
    return true;
}

/*
 * Accept incoming requests from the L2C to snoop cachelines out of the
 * instruction cache and queue them for scheduling.
 */
void ICF::accept_cache_snoops()
{
    while (snoop_port->can_take()) {
        auto snoop_req = snoop_port->take();
        // Track recently snooped lines so we can block incoming fills to them
        snooped_lines.emplace_back(snoop_req->phys_addr, get_cycle());
        snoop_fifo.push_back(snoop_req);
    }
}

/*
 * Invalidate the L1 and L0 instruction caches due to a snoop request from the
 * L2.
 */
void ICF::cache_snoop(MemReq snoop_req)
{
    // Snoop I-cache
    snoop_event(snoop_req, ev_phys_line(snoop_req->phys_addr.line_address().value()));

    auto victim_value = cache.invalidate(snoop_req, snoop_req->phys_addr);
    if (victim_value) {
        snoop_inval_event(snoop_req);
    }

    if (enable_l0_cache)
        l0.invalidate(snoop_req, snoop_req->phys_addr);

    ifb_victim_cache.snoop(snoop_req->phys_addr);
    std::optional<FetchId> youngest_surviving = ifb.snoop(snoop_req->phys_addr, bool(victim_value));
    if (youngest_surviving) {
        snoop_restart_event(snoop_req);
        // If the call to snoop the IFB returned a FetchId, it means we should
        // clear the IFB and ICF pipelines of anything younger than that and
        // re-start the ICF pipelines so we don't violate coherency.
        partial_restart_pipe(youngest_surviving.value());
    }
}

/*
 * Refill the cache array(s). In RTL, the tags and data are filled separately,
 * but we model the two together.
 */
void ICF::cache_refill(Fetch& fetch, bool ifb_was_snooped)
{
    // The L2 might have sent us a line and then immediately snooped it out. To avoid
    // breaking inclusion, don't allow recently snooped lines to fill the cache.
    if (recently_snooped(*fetch->phys_addr))
        return;

    // If this IFB was hit by a snoop, don't let it fill the I-cache to avoid
    // breaking inclusion
    if (ifb_was_snooped)
        return;

    auto pa = *fetch->phys_addr;

    CacheLine line(pa.line_address(), true /*is demand*/, false /*not prefetch*/);
    auto victim_value = cache.refill(fetch, pa, line, false /* suppress event */, pa.line_address().value());
    // ensure L1 is always inclusive of L0
    if (victim_value) {
        auto victim_phys_line = *victim_value;
        if (enable_l0_cache)
            l0.invalidate(fetch, victim_phys_line.addr);
    }
}

/*
 * Accept any incoming responses from the L2C from previous icache misses. Fill
 * the IFB victim cache if all fetches waiting on the corresponding IFB have
 * since been cleared.
 */
void ICF::accept_cache_miss_returns()
{
    while (miss_response->can_take()) {
        auto memresp = miss_response->take();
        auto fbid = memresp->id;

        auto [was_cleared, unclearable, was_snooped] = ifb.handle_response(fbid);

        if (use_ifb_victim_cache && was_cleared && unclearable && !was_snooped) {
            ifb_victim_cache.refill(memresp->phys_addr.line_address());
        }

        if (fake_evict_at_miss && was_cleared) {
            // Model evict-at-miss by filling and then invalidating a response to a cleared IFB entry

            CacheLine line(memresp->phys_addr.line_address(), true, false);
            auto victim_value = cache.refill(memresp,
                                             memresp->phys_addr,
                                             line,
                                             true /* suppress event */,
                                             memresp->phys_addr.line_address().value());
            if (victim_value) {
                auto victim_phys_line = *victim_value;
                if (enable_l0_cache)
                    l0.invalidate(memresp, victim_phys_line.addr);
            }

            cache.invalidate(memresp, memresp->phys_addr.line_address());
            if (enable_l0_cache)
                l0.invalidate(memresp, memresp->phys_addr.line_address());
        }

        // Counts each L2 response, regardless of whether it actually fills IL1
        pmu_event(pmu_num_data(archx::PMU::L1I_CACHE_REFILL));

        // Loop over FEQ, mark each pending fetch with this PA as ready
        for (auto& fetch : feq) {
            if (fetch.second->phys_addr && fetch.second->phys_addr->same_cache_line(memresp->phys_addr)
                && fetch.second->pending)
            {
                AMPERE_ASSERT(!was_cleared, "Cleared IFB completed a fetch (not allowed)");
                fetch.second->missed_icache = false;
                fetch.second->missed_l2icache = false;
                fetch.second->pending = false;
                fetch.second->ready = true;
            }
        }
    }
}

/*
 * Return the next instruction to be fetched for the given FEQ entry by
 * fetching it from the driver, allocating its IQ credit, and initializing
 * its addresses/opcode/flags appropriately.
 */
Instruction ICF::fetch_instruction(Fetch& fetch)
{
    auto pc = fetch->instruction_pc(fetch->instructions_fetched());
    auto pa =
        fetch->phys_addr ? fetch->phys_addr.get() + fetch->instructions_fetched() * 4 : PhysAddr{0xdeadc0dedeadc0de};
    std::shared_ptr<archx::Instruction> archx_instruction;

    fetch->fetch_instruction(pc);
    archx_instruction = driver_port->fetch(pc);
    auto instruction = make_child<Instruction>(fetch, archx_instruction, fetch);
    instruction->fifoid = instruction_fifo.acquire();
    instruction->phys_addr = ignore_itlb_pa ? instruction->archx_instruction->program_counter_pa() : pa;

    // Set the appropriate loop-detector flags on the instruction to signal
    // the IDR to handle it appropriately
    instruction->looping = loop_replay;
    if (fetch->instructions_fetched() == 1)
        instruction->loop_start = fetch->loop_start;
    instruction->loop_abort = fetch->loop_abort;
    if (fetch->instructions_fetched() == fetch->num_instructions())
        instruction->loop_end = fetch->loop_end;

    iq_write_event(instruction,
                   instruction->archx_instruction->program_counter(),
                   inst_bytes(instruction->archx_instruction->opcode()),
                   inst_id_data(instruction->archx_instruction->inum()),
                   fetch_id_data(instruction->fetch->fetch_id));

    // send the fetched instruction to the IDR via the IQ
    iq_write_port->put(instruction);

    if (!wrong_path_this_cycle && instruction->wrong_path()) {
        ev_wrong_path();
        wrong_path_this_cycle = true;
    }

    return instruction;
}

/*
 * Handle generating, sending, and performing a flush for the given branch
 * check
 */
void ICF::generate_branch_check_flush(Fetch& fetch, Instruction& instruction, FlushType flush_type)
{
    auto pc = instruction->pc();
    auto target = fetch->get_prediction().is_taken() ? fetch->get_prediction().target : pc + 4;
    auto flush = make_child<Flush>(instruction, target, pc, fetch, instruction, 0, flush_type);

    branch_check_event(instruction,
                       fetch->program_counter,
                       br_pc(pc),
                       ev_offset(pc.line_offset()),
                       br_target(target),
                       br_type(fetch->get_prediction().branch_type),
                       flush_type_data(flush_type));
    // BPU_BTC_CLEAR and ICR_EARLY_MIS_PRED are both computed in rollup_formula.py as BPU_BTC_CLEAR_BTB_MISS +
    // BPU_BTC_CLEAR_BAD_PREDICTION
    switch (flush_type) {
    case FlushType::BRANCH_CHECK_COND_BRANCH:
        // BPU_CONDITIONAL_BRANCH_MISS_BTB *only* counts when a conditional
        // branch is BTC cleared. The logic is that we don't care about
        // conditional branches which are never taken (and therefore never
        // allocate into the BTB)
        pmu_event(instruction, pmu_num_data(archx::PMU::BPU_CONDITIONAL_BRANCH_MISS_BTB));
    case FlushType::BRANCH_CHECK_UNCOND_BRANCH:
    case FlushType::BRANCH_CHECK_CALL_DIRECT:
    case FlushType::BRANCH_CHECK_CALL_INDIRECT:
    case FlushType::BRANCH_CHECK_UNCOND_INDIRECT:
    case FlushType::BRANCH_CHECK_RETURN:
        pmu_event(instruction, pmu_num_data(archx::PMU::BPU_BTC_CLEAR_BTB_MISS));
        break;
    case FlushType::BRANCH_CHECK_TARGET:
    case FlushType::BRANCH_CHECK_WRONG_TYPE:
        pmu_event(instruction, pmu_num_data(archx::PMU::BPU_BTC_CLEAR_BAD_PREDICTION));
        break;
    default:
        AMPERE_ASSERT(false, "Unexpected flush type for generate_branch_check_flush!");
    }

    pmu_idr_btc_clear->put(true);
    br_check_flush_port->put(flush);
    partial_flush_pipe(fetch->fetch_id);

    // Discard any fetches we were in the process of receiving from the BPU
    // because they are guaranteed to be younger than the fetch we are
    // flushing.
    while (!next_fetches.empty()) {
        auto next_fetch = next_fetches.front();
        if (next_fetch->feq_credit)
            release_credit(next_fetch->feq_credit);
        next_fetches.pop_front();
    }
}

/*
 * Perform a 'branch check' for this instruction (a sanity check that the
 * instruction's opcode is consistent with the BPU's prediction)
 *
 * Return true if the branch check did *not* trigger a flush, false if the
 * branch check triggered a flush
 */
bool ICF::branch_check_flush(Fetch& fetch, Instruction& instruction)
{
    auto flush_type = branch_check(fetch, instruction);

    if (flush_type) {
        generate_branch_check_flush(fetch, instruction, *flush_type);
    }

    return !flush_type;
}

/*
 * Return true if we can write to the IQ. Returning false indicates ID03 should
 * stall this cycle.
 */
bool ICF::calc_can_write_iq()
{
    auto iq_credits = instruction_fifo.get_count();
    if (waiting_for_fifo_drain) {
        if (iq_credits >= fifo_free_watermark && iq_credits >= min_credits_for_iq_write) {
            waiting_for_fifo_drain = false;
        } else {
            iq_watermark_stall_event();
            return false;
        }
    } else {
        if (min_credits_for_iq_write > iq_credits) {
            if (fifo_free_watermark)
                waiting_for_fifo_drain = true;
            return false;
        }
    }
    return true;
}

uint64_t ICF::write_queue(Fetch& fetch, uint64_t max_write_instrs)
{
    // First, ensure we are not writing more instructions than we are allowed
    size_t can_write = fetch->num_instructions() - fetch->instructions_fetched();
    if (can_write > max_write_instrs) {
        if (split_fetches_across_cycles)
            can_write = max_write_instrs;
        else
            return 0;
    }

    // Second, ensure we are not writing beyond a half-cacheline boundary if we
    // are not allowed to
    if (half_cacheline_fetch) {
        auto start_pc = fetch->instruction_pc(fetch->instructions_fetched());
        auto end_pc = start_pc + (can_write - 1) * 4;
        if (half_cacheline_fetch && !start_pc.same_half_cache_line(end_pc)) {
            fetch->two_cycle_iq_write = true;
            can_write = (end_pc.half_line_address() - start_pc) / 4;
        }
    }

    // ICF will only write into the IQ if there is room for all the
    // instructions we are attempting to write. Otherwise, we stall.
    auto iq_credits = instruction_fifo.get_count();
    if (can_write > iq_credits) {
        return 0;
    }

    bool branch_check_exit = false;
    uint64_t instructions_written{0};
    for (uint64_t i = 0; i < can_write; i++) {
        if (!branch_check_exit) {
            auto instruction = fetch_instruction(fetch);
            instructions_written++;

            branch_check_exit = !branch_check_flush(fetch, instruction);
        }

        // This PMU counts even the instructions past the end of a BTC-clear in
        // RTL
        pmu_event(fetch, pmu_num_data(archx::PMU::ICF_INST_SPEC_DECODE));
    }

    if (fetch->instructions_fetched() == fetch->num_instructions() || branch_check_exit) {
        feq.erase(fetch->fetch_id);
        collect_loop_fetches(fetch, branch_check_exit);
    }
    AMPERE_ASSERT(fetch->feq_credit, "Found fetch without an FEQ credit while writing to IQ!");

    return instructions_written;
}

/*
 * Handle collecting instructions(fetches) into the loop buffer when they are
 * written into the IQ
 */
void ICF::collect_loop_fetches(Fetch& fetch, bool branch_check_exit)
{
    if (fetch->loop_abort || (loop_collect && !fetch->loop_collect) || branch_check_exit) {
        loop_abort_event(fetch);
        loop_fetch.clear();
        loop_fetch_index = 0;
        loop_collect = false;
        loop_replay = false;
        restarting_after_loop = false;
    } else if (fetch->loop_start || loop_collect) {
        if (fetch->loop_start) {
            loop_fetch.clear();
            if (fetch->loop_iterations) {
                loop_predicted_iterations = fetch->loop_iterations;
                loop_restart_iterations = fetch->loop_restart_iterations;
            } else {
                loop_predicted_iterations = std::nullopt;
                loop_restart_iterations = std::nullopt;
            }
        }
        AMPERE_ASSERT(fetch->loop_start == loop_fetch.empty(),
                      "The loop buffer must be empty if we are beginning to collect fetches for loop mode");
        if (loop_predicted_iterations)
            loop_collect_event(
                fetch, ev_loop_iterations(*loop_predicted_iterations), ev_restart_iterations(*loop_restart_iterations));
        else
            loop_collect_event(fetch);

        auto new_fetch = make_clone<Fetch>(fetch, fetch, 0);
        loop_fetch.push_back(new_fetch);
        loop_collect = !fetch->loop_end;
        if (!loop_collect) {
            // When entering loop mode (i.e. when setting loop_collect to false
            // because we've found a fetch with fetch->loop_end set) set
            // loop_replay to true and loop_lockout to the number of lockout
            // cycles
            loop_replay = true;
            loop_lockout = loop_start_lockout;
        }

        loop_iterations_seen = 0;
    }
}

/*
 * Write as many instructions as possible to the instruction queue this cycle
 * to simulate loop mode
 */
void ICF::loop_mode_fetch()
{
    // We must wait for the IQ to drain before we switch to loop mode, at
    // which point the IDR must wait an additional cycle
    if (loop_lockout) {
        if (instruction_fifo.all_free())
            loop_lockout--;

        if (!loop_lockout) {
            AMPERE_ASSERT(
                highest_icf_fetch_id > highest_looped_fetch_id,
                "ICF attempting to set looped fetch ID from ICF copy {}, which is lower than current looped value {}",
                highest_icf_fetch_id,
                highest_looped_fetch_id);
            highest_looped_fetch_id = highest_icf_fetch_id;

            // We couldn't update these fetch_id's when we created the clones
            // during loop collection because we didn't know what the highest
            // non-loop-mode fetch ID would be yet. Because we know now,
            // initialize them and notify the BPU.
            for (auto& fetch : loop_fetch) {
                fetch->fetch_id = ++highest_looped_fetch_id;
                loop_fetch_port->put(fetch);
            }
        }
        return;
    }

    size_t instructions_written{0};
    while (instruction_fifo.can_acquire() && !loop_fetch.empty()) {
        auto fetch = loop_fetch[loop_fetch_index];

        uint64_t can_write =
            std::min(fetch->num_instructions() - fetch->instructions_fetched(), instruction_fifo.get_count());

        for (uint64_t i = 0; i < can_write; i++) {
            auto instruction = fetch_instruction(fetch);
            instructions_written++;

            pmu_event(fetch, pmu_num_data(archx::PMU::LOOP_MODE_INST_FETCHED));
        }

        // If we just wrote the last instruction in this fetch, advance our
        // pointer to the next fetch and handle fetch-ending housekeeping tasks
        if (fetch->instructions_fetched() == fetch->num_instructions()) {
            auto prev_loop_fetch_index = loop_fetch_index;
            loop_fetch_index = (loop_fetch_index + 1) % loop_fetch.size();
            if (loop_fetch_index <= prev_loop_fetch_index)
                pmu_event(fetch, pmu_num_data(archx::PMU::LOOP_MODE_LOOPED));
            if (fetch->loop_start)
                loop_iterations_seen++;
            if (fetch->loop_end && loop_restart_iterations && loop_iterations_seen == *loop_restart_iterations) {
                // If we are on the loop-ending branch and have seen the number
                // of iterations before restart, wake up the BPU so it can
                // predict us out of this loop
                const auto& pred = fetch->get_prediction();
                auto restart = make_child<LoopRestart>(fetch,
                                                       fetch->bank,
                                                       fetch->program_counter,
                                                       fetch->prev_program_counter,
                                                       pred.branch_pc,
                                                       pred.target,
                                                       loop_iterations_seen);
                loop_restart->put(restart);
                restarting_after_loop = true;

                // We are re-starting the BPU the end of the loop, so we need
                // to signal to the BPU which fetches we expect to send. This
                // allows the BPU to update its speculative history, and keep
                // the fetch ID's consistent
                AMPERE_ASSERT(loop_predicted_iterations,
                              "Predicted end of loop, but predicted iteration count not set");
                FetchId fetch_id = highest_looped_fetch_id;
                for (size_t iteration = loop_iterations_seen; iteration < *loop_predicted_iterations; iteration++) {
                    for (auto& fetch : loop_fetch) {
                        auto new_fetch = make_clone<Fetch>(fetch, fetch, ++fetch_id);
                        loop_fetch_port->put(new_fetch);
                    }
                }
            }
            if (fetch->loop_end && loop_predicted_iterations && loop_iterations_seen == *loop_predicted_iterations) {
                // If we are on the loop-ending branch and have seen the total
                // number of iterations we are supposed to see, clear the loop
                // buffer so we start normal non-looping execution again
                loop_fetch.clear();
                loop_fetch_index = 0;
                loop_collect = false;
                loop_replay = false;
                restarting_after_loop = false;
            }

            // Because we just read the last instruction in this fetch, replace
            // this index in the loop/fetch buffer with a new clone of it, and
            // send this to the BPU
            if (loop_replay) {
                auto new_fetch = make_clone<Fetch>(fetch, fetch, ++highest_looped_fetch_id);
                if (!restarting_after_loop)
                    loop_fetch_port->put(new_fetch);
                loop_fetch[prev_loop_fetch_index] = new_fetch;
            }
        }
    }

    // Emit the appropriate event for loop prediction or detection writes if we
    // wrote to the IQ this cycle
    if (instructions_written > 0) {
        if (loop_predicted_iterations)
            loop_replay_predicted_event();
        else
            loop_replay_event();
    }
}

/*
 * We received a GPC flush, so flush everything
 */
void ICF::flush()
{
    auto flush = flush_port->read();

    for (auto& fetch : feq) {
        if (fetch.second->feq_credit)
            release_credit(fetch.second->feq_credit);
    }

    loop_fetch.clear();
    loop_fetch_index = 0;
    loop_collect = false;
    loop_replay = false;
    restarting_after_loop = false;

    ifb.flush();
    ifb_victim_cache.flush();
    unacked_itlb_misses.clear();
    itlb_misses.clear();
    tlb_response_queue.clear();
    feq.clear();
    flush_tag_pipe();
    data_pipes.clear();

    if (clear_l0_on_flush)
        l0.clear();

    itlb_faulted = false;
    ifb_younger_hit_stall.reset();
    ifb_allocation_stall = false;

    between_flush_and_first_clear = true;
}

/*
 * Flush everything younger than the FetchID of the clear
 */
void ICF::partial_flush_pipe(FetchId clearing_fetch_id, bool abort_looping)
{
    read_fetch = clearing_fetch_id;

    ifb.clear_from_fetch_id(clearing_fetch_id);
    ifb_victim_cache.clear_from_fetch_id(clearing_fetch_id);
    clear_feq_from_fetch_id(clearing_fetch_id, abort_looping);
    clear_tlb_misses_from_fetch_id<ITLBAckBuffer>(unacked_itlb_misses, clearing_fetch_id);
    itlb_faulted = false;
    clear_tlb_misses_from_fetch_id<ITLBMissBuffer>(itlb_misses, clearing_fetch_id);
    clear_pipes_from_fetch_id(clearing_fetch_id);
    ifb_allocation_stall = false;
}

/*
 * Restart everything younger than the FetchID of the clear (but don't clear
 * the FEQ entries for anything)
 */
void ICF::partial_restart_pipe(FetchId youngest_surviving)
{
    ifb.clear_from_fetch_id(youngest_surviving);
    ifb_victim_cache.clear_from_fetch_id(youngest_surviving);
    restart_feq_from_fetch_id(youngest_surviving);
    clear_tlb_misses_from_fetch_id<ITLBAckBuffer>(unacked_itlb_misses, youngest_surviving);
    itlb_faulted = false;
    clear_tlb_misses_from_fetch_id<ITLBMissBuffer>(itlb_misses, youngest_surviving);
    clear_pipes_from_fetch_id(youngest_surviving, false /*don't invalidate*/);
    ifb_allocation_stall = false;
}

// Reset the state of all FEQ entries younger than `youngest_surviving` and
// clear them from the ICF pipelines so they can restart
void ICF::restart_feq_from_fetch_id(FetchId youngest_surviving)
{
    auto restarting_fetches = feq.upper_bound(youngest_surviving);
    for (auto f = restarting_fetches; f != feq.end(); ++f) {
        f->second->reset_icf_state();
    }
}

void ICF::clear_feq_from_fetch_id(FetchId clearing_fetch_id, bool abort_looping)
{
    auto obsolete_fetches = feq.upper_bound(clearing_fetch_id);

    for (auto f = obsolete_fetches; f != feq.end(); ++f) {
        fetch_cleared_event(f->second, fetch_id_data(f->second->fetch_id));
        release_credit(f->second->feq_credit);
    }

    // Look through remaining fetches after removing those after the clear. If
    // there is still a fetch indicating loop_end, it means this clear was sent
    // by the BPU for fetches *after* it began looping. Most clears should
    // cause the ICF to break out of loop-collection mode, but these may(must)
    // be ignored.
    if (loop_collect && abort_looping) {
        bool found_loop_end = false;
        for (auto& fetch : feq) {
            if (fetch.second->loop_end) {
                found_loop_end = true;
                break;
            }
        }
        if (!found_loop_end) {
            loop_fetch.clear();
            loop_fetch_index = 0;
            loop_collect = false;
            loop_replay = false;
            restarting_after_loop = false;
        }
    }

    feq.erase(obsolete_fetches, feq.end());
}

void ICF::remove_otb_id_from_tlb_response_queue(uint64_t otb_id)
{
    for (auto it = tlb_response_queue.begin(); it != tlb_response_queue.end();) {
        if ((*it)->id == otb_id) {
            it = tlb_response_queue.erase(it);
        } else {
            it++;
        }
    }
}

template<class MB>
void ICF::clear_tlb_misses_from_fetch_id(MB& miss_buffer, FetchId clearing_fetch_id)
{
    for (auto it = miss_buffer.begin(); it != miss_buffer.end();) {
        if (it->second->fetch_id > clearing_fetch_id) {
            itlb_faulted = false;
            if constexpr (std::is_same_v<MB, ITLBMissBuffer>) {
                // If the TLB buffer we are clearing is the actual miss buffer
                // (and not just the 'ack' buffer), additionally clear any
                // responses we have received but not yet processed
                remove_otb_id_from_tlb_response_queue(it->first);
            }
            it = miss_buffer.erase(it);
        } else {
            it++;
        }
    }
}

void ICF::flush_tag_pipe()
{
    for (size_t i = 0; i < tag_pipes.width(); i++) {
        for (size_t j = 0; j < tag_pipes.size(); j++) {
            auto stage = tag_pipes[i][j];
            if (!stage.initialized())
                continue;
            auto& op = stage.get();
            // Avoid flushing TLB invalidations and cache snoops - they must be
            // performed regardless of flushes
            if (op->type != TLB_INVALIDATION && op->type != CACHE_SNOOP) {
                (*op->fetch)->scheduled_it_pipe = false;
                stage.invalidate();
            }
        }
    }
}

void ICF::clear_pipes_from_fetch_id(FetchId clearing_fetch_id, bool invalidate)
{
    if (ifb_younger_hit_stall && clearing_fetch_id < (*ifb_younger_hit_stall)->fetch_id)
        ifb_younger_hit_stall.reset();

    for (size_t i = 0; i < tag_pipes.width(); i++) {
        for (size_t j = 0; j < tag_pipes.size(); j++) {
            auto stage = tag_pipes[i][j];
            if (!stage.initialized())
                continue;
            auto& op = stage.get();
            // TLB invalidations and cache snoops are not flushed here (they
            // will not have `fetch` defined)
            if (op->fetch && (*op->fetch)->fetch_id > clearing_fetch_id) {
                (*op->fetch)->scheduled_it_pipe = false;
                if (invalidate)
                    stage.invalidate();
                else
                    stage.remove();
            }
        }
    }
    for (size_t i = 0; i < data_pipes.width(); i++) {
        for (size_t j = 0; j < data_pipes.size(); j++) {
            auto stage = data_pipes[i][j];
            if (!stage.initialized())
                continue;
            auto& fetch = stage.get();
            if (fetch->fetch_id > clearing_fetch_id) {
                fetch->scheduled_id_pipe = false;
                if (invalidate)
                    stage.invalidate();
                else
                    stage.remove();
            }
        }
    }
}

bool ICF::address_resident(PhysAddr addr) const
{
    return cache.find(addr).has_value();
}

/*
 * get_bank() is a helper to enforce bank conflicts are avoided across ICF
 * pipelines
 *
 * Calculate the icache bank given the PC of a fetch, attempt to add it to a
 * passed-in std::set of banks, and return true if it was a unique insertion
 * into that set.
 */
std::optional<uint64_t> ICF::get_bank(ProgramCounter pc, std::set<uint64_t>& banks)
{
    uint64_t bank = icache_bank_bits ? pc.extract_range(6 + icache_bank_bits, 6) : 0;
    auto [_, unique_bank] = banks.insert(bank);
    if (unique_bank)
        return {bank};
    return {std::nullopt};
}

/*
 * This comparator is used to ensure that the data pipeline with the oldest FEQ
 * in the latest occupied stage is executed first.
 */
bool data_pipeline_comparator(const Pipeline<Fetch>& a, const Pipeline<Fetch>& b)
{
    for (size_t s = a.size() - 1; s > 0; s--) {
        if (a[s].initialized() && b[s].initialized()) {
            return a[s].get()->fetch_id < b[s].get()->fetch_id;
        } else if (a[s].initialized() || b[s].initialized()) {
            return a[s].initialized();
        }
    }
    return true;
}

void ICF::dump(std::ostream& os) const
{
    os << "= ICF ======================================================\n";

    fmt::print("\nIT pipeline: ");
    for (size_t i = 0; i < tag_pipes.width(); i++)
        fmt::print("Pipe {:<25}", i);
    fmt::print("\n");
    for (size_t j = 0; j < tag_pipes.size(); j++) {
        fmt::print(" IT0{}:       ", j + 1);
        for (size_t i = 0; i < tag_pipes.width(); i++) {
            auto stage = tag_pipes[i][j];
            if (stage.initialized()) {
                const auto& op = stage.get();
                fmt::print("{:<30}", op->to_string());
            } else {
                fmt::print("{:<30}", "(empty)");
            }
        }
        fmt::print("\n");
    }

    fmt::print("\nID pipeline: ");
    for (size_t i = 0; i < data_pipes.width(); i++)
        fmt::print("Pipe {:<15}", i);
    fmt::print("\n");
    for (size_t j = 0; j < data_pipes.size(); j++) {
        fmt::print(" ID0{}:       ", j + (trinity_repipeline ? 1 : 0));
        for (size_t i = 0; i < data_pipes.width(); i++) {
            auto stage = data_pipes[i][j];
            if (stage.initialized()) {
                const auto& fetch = stage.get();
                fmt::print("Fetch Id {:<11}", fetch->fetch_id);
            } else {
                fmt::print("{:<20}", "(empty)");
            }
        }
        fmt::print("\n");
    }

    os << "\nFEQ:\n";
    os << " |    TXID    |   Fetch Id   |   Program Counter    | Next Program Counter | iTLB | iCache |\n";
    for (const auto& fetch : feq) {
        fmt::print(" | {:>10} | {:>12} | {:>#20x} | {:>#20x} | {:4} | {:6} |\n",
                   fetch.second->txid().value,
                   fetch.first,
                   fetch.second->program_counter.value(),
                   fetch.second->next_program_counter().value(),
                   fetch.second->phys_addr.is_initialized() ? "Y" : "N",
                   fetch.second->ready ? "Y" : "N");
    }

    if (unacked_itlb_misses.empty() && itlb_misses.empty()) {
        os << "\niTLB Misses: None\n";
    } else {
        os << "\niTLB Misses:\n";
        os << " |   Fetch Id   |    OTB Id \n";
        for (const auto& entry : unacked_itlb_misses)
            fmt::print(" | {:>12} | {:>12}\n", entry.second->fetch_id, "?");
        for (const auto& entry : itlb_misses)
            fmt::print(" | {:>12} | {:>12}\n", entry.second->fetch_id, entry.first);
    }

    fmt::print("itlb_faulted: {}\n", itlb_faulted);

    ifb.dump(os);

    os << "============================================================\n";
}

} // namespace ampere::core

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
