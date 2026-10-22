//
// Copyright (c) 2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include <utility>
#include "ampere/core/l2/l2c.h"
#include "ampere/core/l2/l2c_fb.h"
#include "ampere/core/l2/l2c_line.h"
#include "ampere/core/l2/l2c_opcode.enum.h"
#include "ampere/core/prefetcher.h"
#include "ampere/sc/report.h"
#include "ampere/uarch_common/memop.h"
#include "ampere/uarch_common/memreq.h"

namespace ampere::core {

/** Build a dispatch table indexed by opcode.
 *
 *  To make it easier to understand the different L2 opcodes, their logic is separated
 *  into individual methods. The table is currently sparse; anything we don't model
 *  directs to the "do_unsupported" method, which just reports an error.
 */
void L2C::build_dispatch_table()
{
    dispatch.resize(util::enum_size<l2::Opcode>());
    std::fill(dispatch.begin(), dispatch.end(), &L2C::do_unsupported);

    dispatch[(int)l2::Opcode::BLKRD] = &L2C::do_blkrd;
    dispatch[(int)l2::Opcode::BLKRDUNIQUE] = &L2C::do_blkrdunique;
    dispatch[(int)l2::Opcode::SUBWR] = &L2C::do_subwr;
    dispatch[(int)l2::Opcode::ATOMIC_LOAD] = &L2C::do_atomic;
    dispatch[(int)l2::Opcode::ATOMIC_STORE] = &L2C::do_atomic;
    dispatch[(int)l2::Opcode::ATOMIC_CAS] = &L2C::do_atomic;
    dispatch[(int)l2::Opcode::FILL_FIRSTFLIT] = &L2C::do_fill_firstflit;
    dispatch[(int)l2::Opcode::FILL] = &L2C::do_fill;
    dispatch[(int)l2::Opcode::READ_VICTIM] = &L2C::do_read_victim;
    dispatch[(int)l2::Opcode::READ_DEALLOC] = &L2C::do_read_dealloc;
    dispatch[(int)l2::Opcode::FB_DEALLOC] = &L2C::do_fb_dealloc;
    dispatch[(int)l2::Opcode::SNP_FWD] = &L2C::do_snp_fwd;
    dispatch[(int)l2::Opcode::SNP_INV] = &L2C::do_snp_inv;
    dispatch[(int)l2::Opcode::ALLOC_L3] = &L2C::do_alloc_l3;
}

/** First logical stage of the L2 (read) pipeline.
 */
bool L2C::schedule_read(Pipeline<l2::Op>::Data pipe_op)
{
    if (read_op) {
        if (read_op->opcode == l2::Opcode::BLKRD) {
            switch (read_op->req->origin) {
            case MemOp::Requester::IMMU:
                immu_duplicate_filter.insert(filter_address(read_op->addr));
                break;
            case MemOp::Requester::DMMU:
                dmmu_duplicate_filter.insert(filter_address(read_op->addr));
                break;
            case MemOp::Requester::PREFETCH:
                prefetcher->launch(read_op->req);
                event.pmu(read_op->req, event.pmu_num(archx::PMU::L2_PREFETCH_REQ));
                break;
            default:
                break;
            }
        }

        // deallocate write-specific fb credits early
        if (read_op->fb_credits) {
            release_credit(read_op->fb_credits.write_data_credit);
            release_credit(read_op->fb_credits.split_write_data_credit);
        }

        if (read_op->req && read_op->req->origin == MemOp::Requester::ICF && icf_request_schedule.bind_count() > 0)
            icf_request_schedule->put(read_op->req);

        pipe_op = read_op;
        read_op.reset();
    }

    return true;
}

/** First logical stage of the L2 (write) pipeline.
 */
bool L2C::schedule_write(Pipeline<l2::Op>::Data pipe_op)
{
    if (write_op) {
        lsu_read_prq.clear_pwc_dependencies(write_op->pwc_id);
        pipe_op = write_op;
        write_op.reset();
    }

    return true;
}

/** First logical stage of the L2 (sys only) pipeline.
 */
bool L2C::schedule_sys(Pipeline<l2::Op>::Data pipe_op)
{
    if (sys_op) {
        pipe_op = sys_op;
        sys_op.reset();
    }

    return true;
}

/** Second logical stage of the L2 pipeline.
 *
 *  Do the heavy lifting of executing a particular opcode. This work is
 *  roughly divided into a function per opcode.
 */
bool L2C::process(Pipeline<l2::Op>::Data pipe_op)
{
    l2::Op op = pipe_op.get();

    if (ld_st_ordering_conflict(op)) {
        pipe_op.invalidate();
    } else {
        event.process(op, op->opcode, event.bank(op->bank));
        (this->*dispatch[(int)op->opcode])(pipe_op);
    }

    // If we didn't use our FB credit by now, we're done with it.
    op->fb_credits.release();

    return true;
}

/** Third logical stage of the L2 pipeline.
 *
 *  Send data back to requesters that require it.
 */
bool L2C::writeback(Pipeline<l2::Op>::Data pipe_op)
{
    const auto& op = pipe_op.get();

    // writeback of valid data
    for (auto [req, has_value] : op->return_data) {
        AMPERE_ASSERT(!has_value || op->data_state != LineState::I, "Data is invalid");
        // Only LSU gets speculative responses with invalid data
        if (req->origin == MemOp::Requester::LSU || has_value)
            send_to_core(req, has_value ? op->data_state : LineState::I);

        // speculative wakeups (data is invalid)
        if (!has_value)
            continue;

        event.writeback(req, req->phys_addr);

        // FIXME: this logic and what's in send_to_core() should probably be merged
        switch (req->origin) {
        case MemOp::Requester::LSU:
            set_inclusion_bit(op->addr, true, false);
            set_va_bits(op->addr, req->virt_addr);
            break;
        case MemOp::Requester::ICF:
            set_inclusion_bit(op->addr, true, true);
            break;
        case MemOp::Requester::IMMU:
            immu_duplicate_filter.erase(filter_address(op->addr));
            break;
        case MemOp::Requester::DMMU:
            dmmu_duplicate_filter.erase(filter_address(op->addr));
            break;
        default:
            break;
        }
    }

    return true;
}

bool L2C::is_pwc_write_op(const l2::Op& op) const
{
    if (!op->req)
        return false;

    switch (op->opcode) {
    case l2::Opcode::SUBWR:
        return true;
    case l2::Opcode::ATOMIC_LOAD:
    case l2::Opcode::ATOMIC_STORE:
    case l2::Opcode::ATOMIC_CAS:
        return !op->has_fb() || !fb[op->fbid].srq.comp_rcvd;
    default:
        return false;
    }
}

bool L2C::write_complete(Pipeline<l2::Op>::Data pipe_op)
{
    const auto& op = pipe_op.get();

    // Prior to Connor, this is when we tell the PWC that the write is complete (2 cycles after process)
    if (is_pwc_write_op(op) && !fast_write_complete)
        banks[op->bank].in_flight_stores.update(op->req->barrier_type, op->req->atomic_type, -1);

    return true;
}

/** Final logical stage of the L2 pipeline.
 *
 *  Interact with the outside world (the mesh). Send requests or responses
 *  as required by this operation.
 */
bool L2C::system(Pipeline<l2::Op>::Data pipe_op)
{
    l2::Op op = pipe_op.get();

    if (op->chi.req) {
        sys_req_queue.push(*op->chi.req);
        (*op->chi.req).reset();
    }

    if (op->chi.evict) {
        sys_req_queue.push(*op->chi.evict);
        (*op->chi.evict).reset();
    }

    if (op->chi.rsp) {
        sys_rsp_queue.push(*op->chi.rsp);
        (*op->chi.rsp).reset();
    }

    if (!op->chi.dat.empty()) {
        for (auto& pkt : op->chi.dat)
            sys_dat_queue.push(pkt);

        op->chi.dat.clear();
    }

    return true;
}

void L2C::do_blkrd(Pipeline<l2::Op>::Data pipe_op)
{
    l2::Op& op = pipe_op.get();

    auto l2_hit = lookup_l2(op);
    auto fb_hit = fb.lookup(op->addr);
    bool is_prefetch = op->req->origin == MemOp::Requester::PREFETCH;
    uint64_t set_index = L2C::index(op->addr);

    if (fb_hit) {
        AMPERE_ASSERT(op->req->origin != uarch_common::MemOp::LSU || !fb_hit->atomic(),
                      "LSU requests should recycle before reaching this stage (via ld-st conflict checks)");
        event.fb_hit(op, op->addr);

        if (fb_hit->from_write())
            event.read_hit_write_fb(op, op->addr);

        // A WriteNoSnp request will not get data back, so if a read hits a FB allocated by a WriteNoSnp
        // it has to wait for the FB to deallocate and then try again.
        if (fb_hit->srq.chi_request == mesh::chi::RequestOpcode::WriteNoSnpPtl
            || fb_hit->srq.chi_request == mesh::chi::RequestOpcode::WriteNoSnpFull)
        {
            op->replay_after_fbid = fb_hit->id;
            requeue_request(op, 0);

            event.read_hit_write_nosnp(op, op->addr);
            return;
        }

        if (is_prefetch) {
            prefetcher->drop(op->req,
                             fb_hit->from_prefetch() ? PrefetchDropReason::CACHE_MISS_PREFETCH_IN_FLIGHT
                                                     : PrefetchDropReason::CACHE_MISS_IN_FLIGHT);
            event.pmu(op, event.pmu_num(archx::PMU::L2C_PREFETCH_LFB_HIT));
        } else {
            prefetcher->train(
                set_index, op->req, !prefetch_all_fb_hits, fb_hit->line.prefetch, true, fb_hit->line.prefetch_type);

            // Clear prefetch bit when a demand request hits prefetch-allocated FB entry
            if (fb_hit->line.prefetch && prefetch_clear_on_hit) {
                fb_hit->line.clear_prefetch();
                event.pmu(op, event.pmu_num(archx::PMU::L2_PREFETCH_UPGRADE));
                event.pmu(op->req, event.pmu_num(archx::PMU::L2C_CACHE_FHIT_HWPRF));
            }

            bool completes = fb_hit->state == l2::FillBufferEntry::State::FILLED;
            if (!completes)
                fb_hit->add_response(op->req);
            op->respond(completes ? fb_hit->line.state : LineState::I);

            // We might schedule a fill without a dest and a read at the same time, and then later that
            // same cycle an earlier read hits the fill's FB -- now it has a dest and we are trying to
            // complete two reads at once. This is a real problem faced by RTL, and the solution is that
            // the read that scheduled in parallel with the fill has to get cancelled and retried.
            if (read_op && read_op->scheduled_with_fill && *read_op->scheduled_with_fill == op->addr) {
                requeue_request(read_op, 0);
                banks[op->bank].cancel_schedule(read_op, bank_cancel_busy_mask);
                read_op = nullptr;
            }
        }
    } else if (l2_hit || perfect_hit(op->req->virt_addr)
               || (mmu_always_hit && op->req->origin == MemOp::Requester::DMMU))
    {
        if (is_prefetch) {
            prefetcher->drop(op->req, PrefetchDropReason::CACHE_MISS_NO_RESOURCES);
        } else {
            prefetcher->train(set_index, op->req, true, l2_hit->prefetch, true, l2_hit->prefetch_type);
            if (prefetch_clear_on_hit) {
                if (l2_hit->prefetch)
                    event.pmu(op->req, event.pmu_num(archx::PMU::L2C_CACHE_FHIT_HWPRF));
                l2_hit->clear_prefetch();
            }
            op->respond(l2_hit->state);

            // If the LSU asks for a line which is already in the L1, it's because there is a synonym (two VAs which
            // map to the same PA), and we need to send LSU a snoop to evict the duplicate copy.
            if (enable_synonym_snoops
                && (op->req->origin == MemOp::Requester::LSU || op->req->origin == MemOp::Requester::LSU_PREFETCH)
                && get_l1d(*l2_hit))
            {
                auto snoop_req =
                    make<MemReq>(MemReqObj::snoop(0, op->addr, MemOp::Requester::L2C, MemOp::SnoopType::SNOOP_SYNONYM));
                lsu_snoop_request->put(snoop_req);
                set_inclusion_bit(op->addr, false, false);
                event.lsu_snoop_clear_inclusion(op, op->addr);
            }
        }
    } else {
        auto& fbe = fb_allocate(op, false);

        if (is_prefetch)
            fbe.line.set_prefetch(op->req->prefetch_type);
        else
            prefetcher->train(
                set_index, op->req, false, false, true, op->req->prefetch_type); // Demand load, missing caches

        op->respond_invalid();

        if (int id = fb.has_pending_request(op->addr); id != -1) {
            AMPERE_ASSERT(fb[id].pending_req.has_value() == false, "Pending request is not NULL");
            fb[id].pending_req = gen_sys_req(op,
                                             mesh::chi::RequestOpcode::ReadNotSharedDirty,
                                             op->addr,
                                             (code_is_likely_shared && op->req->origin == MemOp::Requester::ICF));
            event.fb_pending_req(op, event.found_fbid(id), event.phys_line(op->addr.line_address().value()));
        } else {
            op->chi.req = gen_sys_req(op,
                                      mesh::chi::RequestOpcode::ReadNotSharedDirty,
                                      op->addr,
                                      (code_is_likely_shared && op->req->origin == MemOp::Requester::ICF));
        }
        if (parallel_eviction)
            initiate_parallel_eviction(op);

        pmu_refill_events(op);
    }
}

void L2C::do_blkrdunique(Pipeline<l2::Op>::Data pipe_op)
{
    l2::Op& op = pipe_op.get();

    auto l2_hit = lookup_l2(op);
    auto fb_hit = fb.lookup(op->addr);

    if (fb_hit) {
        event.fb_hit(op, op->addr);
        maybe_upgrade(op, *fb_hit);

        if (fb_hit->state == l2::FillBufferEntry::State::FILLED_PENDING_UPGRADE)
            goto blkrdunique_fb_miss;

        fb_hit->add_response(op->req);
        op->respond_invalid();
    } else if ((l2_hit && l2_hit->state != l2::LineState::SC) || perfect_hit(op->req->virt_addr)
               || (mmu_always_hit && op->req->origin == MemOp::Requester::DMMU))
    {
        op->respond(l2_hit->state);
    } else if (enable_perfect_mode || (mmu_always_hit && op->req->origin == MemOp::Requester::DMMU)) {
        op->respond(l2::LineState::UC);
    } else {
    blkrdunique_fb_miss:
        op->respond_invalid();

        fb_allocate(op, false);
        op->chi.req = gen_sys_req(op, mesh::chi::RequestOpcode::ReadUnique, op->addr, false);
        pmu_refill_events(op);
        if (parallel_eviction)
            initiate_parallel_eviction(op);
    }
}

mesh::RequestOpcode L2C::mesh_write_request_opcode(const l2::Op& op, bool hit, bool needs_original_data) const
{
    if (always_nns_memtype && !op->atomic()) {
        if (op->all_bytes_valid())
            return mesh::chi::RequestOpcode::WriteNoSnpFull;
        else
            return mesh::chi::RequestOpcode::WriteNoSnpPtl;
    }

    if (needs_original_data)
        return mesh::chi::RequestOpcode::ReadUnique;

    if (op->all_bytes_valid() && !disable_make_unique)
        return mesh::chi::RequestOpcode::MakeUnique;

    return (hit && use_cleanunique) ? mesh::chi::RequestOpcode::CleanUnique : mesh::chi::RequestOpcode::ReadUnique;
}

void L2C::do_subwr(Pipeline<l2::Op>::Data pipe_op)
{
    l2::Op& op = pipe_op.get();

    auto l2_hit = lookup_l2(op);
    auto fb_hit = fb.lookup(op->addr);

    if (fb_hit) {
        event.fb_hit(op, op->addr);
        maybe_upgrade(op, *fb_hit);

        if (fb_hit->state == l2::FillBufferEntry::State::FILLED_PENDING_UPGRADE)
            goto subwr_fb_miss;

        fb_hit->merge_bytes(op->get_byte_enables());
        if (fb_hit->state != l2::FillBufferEntry::State::PENDING_UPGRADE)
            fb_hit->line.state = l2::LineState::UD;
        fb_hit->is_pwc = true;
        op->data_state = fb_hit->line.state;
    } else if (l2_hit && l2_hit->state != l2::LineState::SC)
        op->data_state = l2_hit->state = l2::LineState::UD;
    else {
    subwr_fb_miss:
        auto& fbe = fb_allocate(op, false);
        op->data_state = fbe.line.state = l2::LineState::UD;
        fbe.is_pwc = true;

        if (l2_hit)
            l2_hit->busy = true;

        auto chi_opcode = mesh_write_request_opcode(op, static_cast<bool>(l2_hit));
        op->chi.req = gen_sys_req(op, chi_opcode, op->addr, false);
        if (parallel_eviction)
            initiate_parallel_eviction(op);
        pmu_refill_events(op);
    }

    // From Connor onwards, notify PWC that write is complete here
    if (fast_write_complete)
        banks[op->bank].in_flight_stores.update(op->req->barrier_type, op->req->atomic_type, -1);
}

void L2C::do_atomic(Pipeline<l2::Op>::Data pipe_op)
{
    l2::Op& op = pipe_op.get();
    if (op->has_fb()) {
        do_fill(pipe_op);
        return;
    }

    auto l2_hit = lookup_l2(op);
    auto fb_hit = fb.lookup(op->addr);
    bool is_load = (op->opcode != l2::Opcode::ATOMIC_STORE);

    AMPERE_ASSERT(!fb_hit, "Atomics should wait till other matching requests are done");

    if (l2_hit && l2_hit->state != l2::LineState::SC) {
        l2_hit->state = l2::LineState::UD;
        if (is_load)
            op->respond(l2_hit->state);
    } else {
        auto& fbe = fb_allocate(op, false);
        fbe.line.state = l2::LineState::UD;
        fbe.is_pwc = true;

        if (is_load)
            op->respond_invalid();

        if (l2_hit)
            l2_hit->busy = true;

        auto chi_opcode = mesh_write_request_opcode(op, static_cast<bool>(l2_hit), is_load);
        op->chi.req = gen_sys_req(op, chi_opcode, op->addr, false);
        pmu_refill_events(op);
        if (parallel_eviction)
            initiate_parallel_eviction(op);
    }

    // From Connor onwards, notify PWC that write is complete here
    if (fast_write_complete)
        banks[op->bank].in_flight_stores.update(op->req->barrier_type, op->req->atomic_type, -1);
}

void L2C::do_fill_firstflit(Pipeline<l2::Op>::Data pipe_op)
{
    pipe_op.invalidate(); // end of the pipeline
}

void L2C::do_fill(Pipeline<l2::Op>::Data pipe_op)
{
    l2::Op& op = pipe_op.get();
    auto& fbe = fb[op->fbid];
    uint64_t set_index = L2C::index(op->addr);

    if (fbe.srq.need_comp_ack && !fbe.srq.sent_comp_ack) {
        op->chi.rsp =
            make_clone<mesh::MeshRsp>(op, node_id, fbe.srq.tgt, fbe.srq.dbid, mesh::chi::ResponseOpcode::CompAck);
        fbe.srq.sent_comp_ack = true;
    }

    if (!is_unique(fbe.line.state))
        event.refill_shared(op, op->addr);

    if (fbe.state == l2::FillBufferEntry::State::PENDING_UPGRADE) {
        if (!is_unique(fbe.line.state)) {
            // "This is not the FILL you're looking for..."
            //
            // We'll need this line again in UC state to satisfy some write that came
            // along after this FB was first allocated.
            //
            // FIXME: What to do with PMUs here?
            //
            fbe.line.state = l2::LineState::UD;
            if (op->all_bytes_valid() && !disable_make_unique)
                op->chi.req = gen_sys_req(op, mesh::chi::RequestOpcode::MakeUnique, op->addr, false);
            else
                op->chi.req = gen_sys_req(op, mesh::chi::RequestOpcode::ReadUnique, op->addr, false);

            return;
        } else {
            fbe.line.state = l2::LineState::UD;
        }
    }

    op->data_state = fbe.line.state;

    // A write op hit L2 in shared state and we sent out a cleanunique to upgrade to unique
    // but the line turned out to be invalid during fill -> line was snooped out.
    // Issue a readunique to obtain the data in unique state.
    if (fbe.srq.chi_request == mesh::chi::RequestOpcode::CleanUnique
        && banks[op->bank].busy_snoop_invalidated(op->addr))
    {
        op->chi.req = gen_sys_req(op, mesh::chi::RequestOpcode::ReadUnique, op->addr, false);
        return;
    }

    bool can_writeback = op->atomic() || !ld_st_ordering_conflict_at_fill(op->addr, fbe);
    op->respond_to_all(fbe.return_data, can_writeback);

    if (fbe.line.cache_type == CacheType::L2)
        prefetcher->fill(set_index,
                         fbe.allocated_by_req,
                         fbe.line.prefetch,
                         fbe.allocated_by_req->origin == MemOp::Requester::PREFETCH);

    if (fbe.line.cache_type == CacheType::L3)
        AMPERE_ASSERT((unsigned)op->bank == banks.select(fbe.line.addr),
                      "Bank id of the stash's fill is different from the original snoop addr's bank id.\n");

    // Release fill_address credit here to ensure that subsequent fb_deallocate cases in this method see we're done
    release_credit(fbe.credits.fill_address_credit);

    // A fill for a write may hit another upgraded FB entry for the same address.
    // Or if we're in never_fill mode, we're just done with the FB at this point
    if (never_fill || fb.overwrite_upgraded_fb_entry(op->fbid)) {
        release_credit(fbe.credits.eviction_address_credit);
        release_credit(fbe.credits.eviction_data_credit);
        release_credit(fbe.credits.split_eviction_data_credit);
        if (fbe.credits.write_data_credit)
            release_credit(fbe.credits.write_data_credit);
        if (fbe.credits.split_write_data_credit)
            release_credit(fbe.credits.split_write_data_credit);
        fb_deallocate(op);
        return;
    }

    if (parallel_eviction) {
        swap_victim(op);
    } else {
        auto [victim, invalidate_l1i] = banks[op->bank].fill_or_select_victim(fbe.line, fbe.allocated_by_req);

        // IL1 is snooped (legacy code for likely_shared)
        if (invalidate_l1i) {
            auto snoop_req =
                make<MemReq>(MemReqObj::snoop(0, fbe.line.addr, MemOp::Requester::L2C, MemOp::SNOOP_IN_L1));
            icf_snoop_request->put(snoop_req);
            set_inclusion_bit(fbe.line.addr, false, true);
        }

        if (!victim) {
            release_credit(fbe.credits.eviction_address_credit);
            release_credit(fbe.credits.eviction_data_credit);
            release_credit(fbe.credits.split_eviction_data_credit);
            if (fbe.credits.write_data_credit)
                release_credit(fbe.credits.write_data_credit);
            if (fbe.credits.split_write_data_credit)
                release_credit(fbe.credits.split_write_data_credit);
            fb_deallocate(op);
        } else {
            fbe.state = l2::FillBufferEntry::State::FILLED;
            fbe.victim = true;
            fbe.victim_way = victim->way;

            auto dataless_eviction = generate_eviction(op, victim->addr, victim->state, victim->prefetch);

            // For a dataless eviction, or if we swap the incoming data and the victim right away,
            // the fill (and any resulting snoop) happens right here. For evict with data,
            // the fill doesn't actually happen until we read the victim out of the cache.
            if (evict_at_fill)
                swap_victim(op);
            if (dataless_eviction)
                replace_line(op);
        }
    }

    // FILL might be the final op of a transaction if we have done parallel eviction,
    // so clean up fb resources here if necessary
    if (parallel_eviction)
        finalize_fb(op);

    event.refill(op, event.l2(fbe.line.cache_type == CacheType::L2), op->addr);
}

bool L2C::generate_eviction(const l2::Op& op, PhysAddr victim_addr, l2::LineState victim_state, bool victim_prefetch)
{
    auto& fbe = fb[op->fbid];
    bool is_l2 = fbe.line.cache_type == CacheType::L2;

    mesh::RequestOpcode chi_opcode;

    if (victim_state == l2::LineState::UD) {
        chi_opcode = mesh::chi::RequestOpcode::WriteBackFull;
    } else if (victim_state == l2::LineState::UC
               && ((is_l2 && (prefetch_evict_full || !victim_prefetch) && l2_write_back_clean)
                   || (!is_l2 && l3_write_back_clean)))
    {
        chi_opcode = mesh::chi::RequestOpcode::WriteEvictFull;
    } else {
        chi_opcode = mesh::chi::RequestOpcode::Evict;
        fbe.state = l2::FillBufferEntry::State::DEALLOC_READY;
    }

    // We do not store how the prefetch was filled, for now passing false.
    // This parameter is currently not used for any l2 prefetchers
    if (is_l2)
        prefetcher->evict(fbe.line.addr, fbe.line.prefetch, false);

    event.eviction(op, event.l2(is_l2), victim_addr);
    if (chi_opcode == mesh::chi::RequestOpcode::WriteEvictFull || chi_opcode == mesh::chi::RequestOpcode::WriteBackFull)
        event.eviction_data(event.l2(is_l2));

    op->chi.evict = gen_sys_req(op, fbe, chi_opcode, victim_addr);

    if (chi_opcode == mesh::chi::RequestOpcode::Evict) {
        release_credit(fbe.credits.eviction_data_credit);
        release_credit(fbe.credits.split_eviction_data_credit);

        return true;
    } else {
        // FIXME: What is the distinction between these?
        event.pmu(op, event.pmu_num(archx::PMU::L2D_CACHE_WB));
        event.pmu(op, event.pmu_num(archx::PMU::L2D_CACHE_WB_VICTIM));
    }

    return chi_opcode == mesh::chi::RequestOpcode::Evict;
}

void L2C::initiate_parallel_eviction(l2::Op& op)
{
    auto& fbe = fb[op->fbid];

    auto [hit, victim_way, victim, invalidate_l1i] =
        banks[op->bank].reserve_fill_target(fbe.line, fbe.allocated_by_req);

    // Not sure this should happen at parallel eviction time but current
    // L2 model infrastructure restricts our ability to easily do this
    // elsewhere
    // IL1 is snooped (legacy code for likely_shared)
    if (invalidate_l1i) {
        auto snoop_req = make<MemReq>(MemReqObj::snoop(0, fbe.line.addr, MemOp::Requester::L2C, MemOp::SNOOP_IN_L1));
        icf_snoop_request->put(snoop_req);
        set_inclusion_bit(fbe.line.addr, false, true);
    }

    // Always save the "victim" way. The cases are:
    // 1. Hit but upgrading SC line to Unique, save hit way as "victim"
    // 2. No hit but filling invalid way (no "victim" but way is reserved for us)
    // 3. No hit and victim

    fbe.victim_way = victim_way;

    if (victim) {
        event.eviction(op, event.l2(true), victim->addr);

        auto& eviction_fbe = fb_allocate(op, false, true);

        // put victim line info into eviction data fb
        eviction_fbe.victim = true;
        eviction_fbe.line.state = victim->state;
        eviction_fbe.line.addr = victim->addr;
        eviction_fbe.line.in_l1i = victim->in_l1i;
        eviction_fbe.line.in_l1d = victim->in_l1d;
        eviction_fbe.line.prefetch = victim->prefetch;
        eviction_fbe.state = l2::FillBufferEntry::State::EVICTING;

        // This is a bit tricky since the op was associated with the original fill-only FB earlier in this method.
        // But the fb_allocate call re-associated the op with the new eviction-only FB. So we can directly
        // call eviction related methods on this same op and it will do the correct thing even though the
        // original FB doesn't have the correct victim info in the parallel eviction case.
        auto dataless_eviction = generate_eviction(op, victim->addr, victim->state, victim->prefetch);

        if (!dealloc_back_invalidates || dataless_eviction)
            back_invalidate_victim(op);
    } else {
        release_credit(op->fb_credits.eviction_address_credit);
        release_credit(op->fb_credits.eviction_data_credit);
        release_credit(op->fb_credits.split_eviction_data_credit);
    }
}

void L2C::swap_victim(const l2::Op& op)
{
    auto& fbe = fb[op->fbid];

    fbe.state = l2::FillBufferEntry::State::EVICTING;

    // The L2C snoops an L1 at the last possible moment, just before it actually removes the line from the cache.
    banks[op->bank].replace(fbe.line, fbe.victim_way, fbe.allocated_by_req);

    if (!parallel_eviction && !dealloc_back_invalidates)
        back_invalidate_victim(op);

    if (fbe.credits.write_data_credit)
        release_credit(fbe.credits.write_data_credit);
    if (fbe.credits.split_write_data_credit)
        release_credit(fbe.credits.split_write_data_credit);
}

void L2C::back_invalidate_victim(const l2::Op& op)
{
    auto& fbe = fb[op->fbid];

    if (fbe.victim) {
        AMPERE_ASSERT(
            fbe.state == l2::FillBufferEntry::State::DEALLOC_READY || fbe.state == l2::FillBufferEntry::State::EVICTING,
            "Back-invalidating non-victim line from fillbuffer");

        auto victim_addr = fbe.line.addr;
        auto in_l1d = fbe.line.in_l1d;
        auto in_l1i = fbe.line.in_l1i;

        // Every eviction snoops the LSU, which needs to know whether the line might be in DL1 or only needs to snoop
        // the L

        auto dl1_snoop_req =
            make<MemReq>(MemReqObj::snoop(0,
                                          victim_addr,
                                          MemOp::Requester::L2C,
                                          in_l1d ? MemOp::SnoopType::SNOOP_IN_L1 : MemOp::SnoopType::SNOOP_OTHER));

        lsu_snoop_request->put(dl1_snoop_req);

        // IL1 is only snooped if its L1-present bit was set
        if (in_l1i) {
            auto il1_snoop_req =
                make<MemReq>(MemReqObj::snoop(0, victim_addr, MemOp::Requester::L2C, MemOp::SnoopType::SNOOP_IN_L1));
            icf_snoop_request->put(il1_snoop_req);
        }
    }
}

void L2C::replace_line(const l2::Op& op)
{
    if (!evict_at_fill && !parallel_eviction)
        swap_victim(op);
    if (dealloc_back_invalidates)
        back_invalidate_victim(op);
}

void L2C::do_read_victim(Pipeline<l2::Op>::Data pipe_op)
{
    l2::Op& op = pipe_op.get();
    auto& fbe = fb[op->fbid];

    replace_line(op);
    fbe.state = l2::FillBufferEntry::State::DEALLOC_READY;

    op->chi.dat = make_data(op, node_id, fbe.srq.tgt, fbe.srq.dbid, mesh::chi::DataOpcode::CopyBackWrData);

    // Pre-Domino: home node only checks the pass_dirty field to handle evictions.
    // Domino: both the forwarding state and pass dirty are needed to stash evicted lines.
    for (auto& pkt : op->chi.dat)
        pkt->Resp = cache_state_to_resp_status(fbe.line.state);

    AMPERE_ASSERT(fbe.credits.eviction_data_credit, "eviction_data credit already released ...");
    release_credit(fbe.credits.eviction_data_credit);
    release_credit(fbe.credits.split_eviction_data_credit);
}

void L2C::do_read_dealloc(Pipeline<l2::Op>::Data pipe_op)
{
    l2::Op& op = pipe_op.get();
    auto& fbe = fb[op->fbid];

    lsu_read_prq.clear_fb_dependencies(op->fbid);

    replace_line(op);
    op->chi.dat = make_data(op, node_id, fbe.srq.tgt, fbe.srq.dbid, mesh::chi::DataOpcode::CopyBackWrData);

    // Pre-Domino: home node only checks the pass_dirty field to handle evictions.
    // Domino: both the forwarding state and pass dirty are needed to stash evicted lines.
    for (auto& pkt : op->chi.dat)
        pkt->Resp = cache_state_to_resp_status(fbe.line.state);

    fb.srt_deallocate(fbe);

    release_credit(fbe.credits.eviction_data_credit);
    release_credit(fbe.credits.split_eviction_data_credit);

    finalize_fb(op);
}

void L2C::do_fb_dealloc(Pipeline<l2::Op>::Data pipe_op)
{
    l2::Op& op = pipe_op.get();

    if (finalize_fb(op)) {
        pipe_op.invalidate(); // end of the pipeline
    }
}

void L2C::do_snp_inv(Pipeline<l2::Op>::Data pipe_op)
{
    l2::Op& op = pipe_op.get();

    auto l2_hit = banks[op->bank].lookup(op->addr, false);
    auto fb_hit = fb.lookup(op->addr);

    bool in_l1d{false};
    bool in_l1i{false};
    bool snoop_lsu{false};
    l2::LineState line_state{l2::LineState::I};

    AMPERE_ASSERT(op->snp->Opcode == mesh::chi::SnoopOpcode::SnpUnique
                      || op->snp->Opcode == mesh::chi::SnoopOpcode::SnpCleanInvalid
                      || op->snp->Opcode == mesh::chi::SnoopOpcode::SnpMakeInvalid,
                  "Unhandled Snoop {}",
                  op->snp->Opcode);

    if (fb_hit && fb_hit->state == l2::FillBufferEntry::State::FILLED) { // FIXME: which states matter here?
        event.fb_hit(op, op->addr);
        in_l1d = get_l1d(fb_hit->line);
        in_l1i = get_l1i(fb_hit->line);
        snoop_lsu = true;
        line_state = fb_hit->line.state;
        fb_hit->line.state = l2::LineState::I;
    } else if (l2_hit) {
        in_l1d = get_l1d(*l2_hit);
        in_l1i = get_l1i(*l2_hit);
        line_state = l2_hit->state;
        snoop_lsu = true;
        l2_hit->state = l2::LineState::I;
    }

    // Every eviction snoops the LSU, which needs to know if this line was in the DL1
    // Note that the id of this request is 0. I believe that's okay because we don't
    // actually expect a response, so we don't need to match anything up.
    if (snoop_lsu) {
        auto dl1_snoop_req =
            make<MemReq>(MemReqObj::snoop(0,
                                          l2_hit->addr,
                                          MemOp::Requester::L2C,
                                          in_l1d ? MemOp::SnoopType::SNOOP_IN_L1 : MemOp::SnoopType::SNOOP_OTHER));
        lsu_snoop_request->put(dl1_snoop_req);
    }

    // IL1 is only snooped if its L1-present bit was set
    if (in_l1i) {
        auto il1_snoop_req =
            make<MemReq>(MemReqObj::snoop(0, l2_hit->addr, MemOp::Requester::L2C, MemOp::SnoopType::SNOOP_IN_L1));
        icf_snoop_request->put(il1_snoop_req);
    }

    if (op->snp->Opcode == mesh::chi::SnoopOpcode::SnpUnique) {
        if (op->snp->RetToSrc && line_state != l2::LineState::I) {
            op->chi.dat = make_data(op, node_id, op->snp->SrcID, op->snp->TxnID, mesh::chi::DataOpcode::SnpRespData);
            for (auto& pkt : op->chi.dat)
                pkt->Resp = is_dirty(line_state) ? mesh::chi::RespSnoop_I_PD : mesh::chi::RespSnoop_I;
        } else {
            if (is_dirty(line_state)) {
                op->chi.dat =
                    make_data(op, node_id, op->snp->SrcID, op->snp->TxnID, mesh::chi::DataOpcode::SnpRespData);
                for (auto& pkt : op->chi.dat)
                    pkt->Resp = mesh::chi::RespSnoop_I_PD;
            } else {
                op->chi.rsp = make_clone<mesh::MeshRsp>(
                    op->snp, node_id, op->snp->SrcID, op->snp->TxnID, mesh::chi::ResponseOpcode::SnpResp);
                (*op->chi.rsp)->Resp = mesh::chi::RespSnoop_I;
            }
        }
    } else if (op->snp->Opcode == mesh::chi::SnoopOpcode::SnpCleanInvalid) {
        if (is_dirty(line_state)) {
            op->chi.dat = make_data(op, node_id, op->snp->SrcID, op->snp->TxnID, mesh::chi::DataOpcode::SnpRespData);
            for (auto& pkt : op->chi.dat)
                pkt->Resp = mesh::chi::RespSnoop_I_PD;
        } else {
            op->chi.rsp = make_clone<mesh::MeshRsp>(
                op->snp, node_id, op->snp->SrcID, op->snp->TxnID, mesh::chi::ResponseOpcode::SnpResp);
            (*op->chi.rsp)->Resp = mesh::chi::RespSnoop_I;
        }
    } else if (op->snp->Opcode == mesh::chi::SnoopOpcode::SnpMakeInvalid) {
        op->chi.rsp = make_clone<mesh::MeshRsp>(
            op->snp, node_id, op->snp->SrcID, op->snp->TxnID, mesh::chi::ResponseOpcode::SnpResp);
        (*op->chi.rsp)->Resp = mesh::chi::RespSnoop_I;
    }
}

void L2C::do_snp_fwd(Pipeline<l2::Op>::Data pipe_op)
{
    l2::Op& op = pipe_op.get();
    auto l2_hit = banks[op->bank].lookup(op->addr, false);
    auto fb_hit = fb.lookup(op->addr);
    Line line;
    mesh::chi::ResponseStatus resp_status;
    bool hit = false;

    if (fb_hit && fb_hit->state == l2::FillBufferEntry::State::FILLED) {
        line = fb_hit->line;
        hit = true;
    } else if (l2_hit) {
        line = *l2_hit;
        hit = true;
    }

    AMPERE_ASSERT(op->snp->Opcode == mesh::chi::SnoopOpcode::SnpCleanFwd
                      || op->snp->Opcode == mesh::chi::SnoopOpcode::SnpUniqueFwd,
                  "Unhandled SnoopFwd {}",
                  op->snp->Opcode);

    if (op->snp->Opcode == mesh::chi::SnoopOpcode::SnpCleanFwd) {
        if (hit) {
            // for snoop response to PxHA; final state of the cacheline at snoopee
            // FIXME: handle DoNotGoToSD
            resp_status = is_dirty(line.state) ? mesh::chi::RespSnoop_SD : mesh::chi::RespSnoop_SC;
            line.state = is_dirty(line.state) ? LineState::SD : LineState::SC;

            // Send Response back to the PxHA stating RN is going to forward data to the requesting RN
            op->chi.rsp = make_clone<mesh::MeshRsp>(
                op->snp, node_id, op->snp->SrcID, op->snp->TxnID, mesh::chi::ResponseOpcode::SnpRespFwded);
            (*op->chi.rsp)->Resp = resp_status;
            (*op->chi.rsp)->FwdState = mesh::chi::RespComp_SC;

            // Create CompData Request -> always send SC state to the requester
            op->chi.dat = make_data(op, node_id, op->snp->FwdNID, op->snp->FwdTxnID, mesh::chi::DataOpcode::CompData);
            for (auto& pkt : op->chi.dat) {
                pkt->Resp = mesh::chi::RespComp_SC;
                pkt->DBID = op->snp->TxnID;
                pkt->HomeNID = op->snp->SrcID;
            }
        } else {
            // It is possible to miss both caches, even though we don't do silent evictions, because
            // we might have independently decided to evict this line before receiving the snoop
            // (i.e., there's an Evict or some-such in flight right now).
            //
            // In that case we just have to tell the PxHA that we don't have what it thought we had.
            //
            op->chi.rsp = make_clone<mesh::MeshRsp>(
                op->snp, node_id, op->snp->SrcID, op->snp->TxnID, mesh::chi::ResponseOpcode::SnpResp);
            op->chi.rsp->get()->Resp = mesh::chi::RespSnoop_I;
        }
    } else if (op->snp->Opcode == mesh::chi::SnoopOpcode::SnpUniqueFwd) {
        if (hit) {
            if (is_dirty(line.state))
                resp_status = mesh::chi::RespComp_UD_PD;
            else
                resp_status = mesh::chi::RespComp_UC;

            line.state = LineState::I;

            // Send Response back to the PxHA stating RN is going to forward data to the requesting RN
            op->chi.rsp = make_clone<mesh::MeshRsp>(
                op->snp, node_id, op->snp->SrcID, op->snp->TxnID, mesh::chi::ResponseOpcode::SnpRespFwded);
            op->chi.rsp->get()->Resp = mesh::chi::RespSnoop_I;
            op->chi.rsp->get()->FwdState = resp_status;

            // Create CompData Request
            op->chi.dat = make_data(op, node_id, op->snp->FwdNID, op->snp->FwdTxnID, mesh::chi::DataOpcode::CompData);
            for (auto& pkt : op->chi.dat) {
                pkt->Resp = resp_status;
                pkt->DBID = op->snp->TxnID;
                pkt->HomeNID = op->snp->SrcID;
            }
        } else {
            // It is possible to miss both caches, even though we don't do silent evictions, because
            // we might have independently decided to evict this line before receiving the snoop
            // (i.e., there's an Evict or some-such in flight right now).
            //
            // In that case we just have to tell the PxHA that we don't have what it thought we had.
            //
            op->chi.rsp = make_clone<mesh::MeshRsp>(
                op->snp, node_id, op->snp->SrcID, op->snp->TxnID, mesh::chi::ResponseOpcode::SnpResp);
            op->chi.rsp->get()->Resp = mesh::chi::RespSnoop_I;
        }
    }
}

void L2C::do_unsupported(Pipeline<l2::Op>::Data pipe_op)
{
    l2::Op& op = pipe_op.get();
    AMPERE_ERROR("L2 opcode {} is not yet supported", (int)op->opcode); // FIXME: opcode to string
}

void L2C::maybe_upgrade(l2::Op& op, l2::FillBufferEntry& fbe)
{
    if (fbe.line.state != l2::LineState::I && fbe.line.state != l2::LineState::SC)
        return;

    if (op->atomic()) {
        if (fbe.state == l2::FillBufferEntry::State::ALLOCATED && !fbe.is_pwc)
            fbe.state = l2::FillBufferEntry::State::PENDING_UPGRADE;
    } else {
        if (fbe.state == l2::FillBufferEntry::State::ALLOCATED)
            fbe.state = l2::FillBufferEntry::State::PENDING_UPGRADE;
        else if (fbe.state == l2::FillBufferEntry::State::FILLED)
            fbe.state = l2::FillBufferEntry::State::FILLED_PENDING_UPGRADE;
        else if (fbe.state != l2::FillBufferEntry::State::PENDING_UPGRADE)
            AMPERE_ERROR("trying to upgrade FB in state {}", (int)fbe.state);
    }
}

l2::RequestQueue* L2C::prq_for_request(const l2::Op op)
{
    if (op->req) {
        switch (op->req->origin) {
        case MemOp::Requester::IMMU:
            return &immu_prq;
        case MemOp::Requester::ICF:
            return &icf_prq;
        case MemOp::Requester::DMMU:
            return &dmmu_prq;
        case MemOp::Requester::LSU:
            return &lsu_read_prq;
        case MemOp::Requester::PREFETCH:
            return &prefetch_prq;
        default:
            AMPERE_ERROR("No PRQ known for op: origin {}", std::to_underlying(op->req->origin));
            return nullptr;
        }
    } else {
        switch (op->opcode) {
        case l2::Opcode::READ_DEALLOC:
            return &srq_dealloc;
        default:
            AMPERE_ERROR("No PRQ known for op: no request, opcode {}", op->opcode);
            return nullptr;
        }
    }
}

void L2C::requeue_request(l2::Op op, uint64_t pwc_id)
{
    if (pwc_id)
        op->pwc_id = pwc_id;

    // Note that this does not necessarily return the op to its original position in the queue. This is
    // because we don't have a real OFB, just a FIFO. But I don't *think* it should matter, as loads can
    // go out of order and the LSU should not send us more than only load to the same cache line.
    //
    auto* prq = prq_for_request(op);
    prq->insert_front(op);
}

bool L2C::must_replay_for_store_conflicts(MemOp::Requester requester)
{
    return requester == uarch_common::MemOp::LSU;
}

bool L2C::must_replay_for_store_conflicts(const l2::Op& op)
{
    return (op->opcode == l2::Opcode::BLKRD || op->opcode == l2::Opcode::BLKRDUNIQUE)
           && must_replay_for_store_conflicts(op->req->origin);
}

bool L2C::ld_st_ordering_conflict(l2::Op& op)
{
    if (must_replay_for_store_conflicts(op)) {
        std::optional<uint64_t> block_id;
        if (auto pwc_id = pwc.check_partial_address_match(op->addr); pwc_id) {
            block_id = pwc_id;
        } else if (auto& bank = banks[op->bank]; bank.in_flight_stores.atomic || fb.pending_atomic(op->bank)) {
            block_id = 0;
        }

        if (block_id) {
            requeue_request(op, *block_id);
            return true;
        }
    }

    return false;
}

bool L2C::ld_st_ordering_conflict_at_fill(PhysAddr addr, FillBufferEntry& fbe)
{
    bool pwc_check_required = std::any_of(fbe.return_data.begin(), fbe.return_data.end(), [](const auto& req) {
        return must_replay_for_store_conflicts(req->origin);
    });
    if (!pwc_check_required)
        return false;

    auto pwc_id = pwc.check_partial_address_match(addr);
    if (!pwc_id)
        return false;

    for (auto& req : fbe.return_data) {
        if (must_replay_for_store_conflicts(req->origin)) {
            requeue_request(memreq_to_op(req), pwc_id);
            event.pwc_hit_at_refill(req);
        }
    }

    return true;
}

bool L2C::finalize_fb(l2::Op& op)
{
    bool op_complete = true;

    if (fb[op->fbid].pending_req) {
        op->chi.req = fb[op->fbid].pending_req;
        fb[op->fbid].pending_req.reset();
        op_complete = false;
    }

    fb_deallocate(op);

    return op_complete;
}

void L2C::do_alloc_l3(Pipeline<l2::Op>::Data pipe_op)
{
    l2::Op& op = pipe_op.get();

    if (!op->fb_credits
        || fb.lookup(op->addr)) { // rejected stash: fill buffer not available or stash snoop hits fill buffer
        op->chi.rsp =
            make_clone<mesh::MeshRsp>(op, node_id, op->snp->SrcID, op->snp->TxnID, mesh::chi::ResponseOpcode::SnpResp);
        (*op->chi.rsp)->Resp = mesh::chi::RespSnoop_I;
        event.stash_reject();
        return;
    }

    // Create a new Fill buffer entry
    auto& fbe = fb_allocate(op, true);
    fbe.line.cache_type = CacheType::L3;
    op->chi.rsp = make<mesh::MeshRsp>(node_id, op->snp->SrcID, op->snp->TxnID, mesh::chi::ResponseOpcode::SnpResp);
    (*op->chi.rsp)->Resp = mesh::chi::RespSnoop_I;
    (*op->chi.rsp)->DBID = fbe.id;
    (*op->chi.rsp)->DatPull = DataPull::Read;

    if (parallel_eviction)
        initiate_parallel_eviction(op);

    // A stash that triggers a data pull needs to allocate an SRT entry.
    // The SRT entry would be active until the completion of the data pull.
    fb.srt_allocate(fbe.id);
    fbe.srq.addr = op->addr;
    fbe.srq.tgt = op->snp->SrcID;  // tgt for CompAck to PxHA
    fbe.srq.dbid = op->snp->TxnID; // txnId for CompAck to PxHA
    fbe.srq.need_comp_ack = true;
}

mesh::chi::ResponseStatus L2C::cache_state_to_resp_status(l2::LineState cache_state)
{
    switch (cache_state) {
    case l2::LineState::UD:
        return mesh::chi::RespWriteData_UD_PD;
    case l2::LineState::SD:
        return mesh::chi::RespWriteData_SD_PD;
    case l2::LineState::UC:
        return mesh::chi::RespWriteData_UC;
    case l2::LineState::SC:
        return mesh::chi::RespWriteData_SC;
    default:
        return mesh::chi::RespWriteData_I;
    }
}

} // namespace ampere::core
