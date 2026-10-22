//
// Copyright (c) 2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
// TODO: way reservation
// TODO: direct write (what is this? it is not in the L2 spec)
// TODO: bank set-based scheduling (is this real?)
//
#include "ampere/archx/pmu.h"
#include "ampere/core/l2/l2c.h"
#include <utility>
#include "ampere/core/l2/l2c_bank.h"
#include "ampere/core/l2/l2c_fb.h"
#include "ampere/core/l2/l2c_line.h"
#include "ampere/core/l2/l2c_op.h"
#include "ampere/core/l2/l2c_opcode.enum.h"
#include "ampere/core/prefetchers/oracle.h"
#include "ampere/memory_stream/memory_stream.h"
#include "ampere/uarch_common/memop.h"

namespace ampere::core {

using uarch_common::MemOp::StoreType;

L2C::L2C(const sc_core::sc_module_name& name) : MeshModule(name)
{
    warmup_uop_source->disable(); // UopSourceProg module is used only for warmup for as a Uop source
    build_requests_by_priority();
    build_dispatch_table();

    connect(prefetcher->prefetch_request, prefetch_request).name("prefetch_request_port");
    bool prefetcher_using_oracle = OraclePrefetcher<MemReq>::try_setup(prefetcher.get(), oracle_driver);

    for (uint64_t b = 0; b < num_banks; b++) {
        banks[b].l1i_lookup.bind(l1i_lookup);
        banks[b].l1d_lookup.bind(l1d_lookup);
        banks[b].perfect_l1i_bits = perfect_l1i_bits;
        banks[b].perfect_l1d_bits = perfect_l1d_bits;
        event.fb_used.push_back(
            new Event(fmt::format("fb_used_{}", b), fmt::format("Utilization of fill buffer {}", b)));
        fb_used.push_back(0);
    }

    banks[0].crp->l1i_lookup.bind(l1i_lookup);
    banks[0].crp->l1d_lookup.bind(l1d_lookup);
    banks[0].crp->perfect_l1i_bits = perfect_l1i_bits;
    banks[0].crp->perfect_l1d_bits = perfect_l1d_bits;

    auto* oracle_crp_ptr = dynamic_cast<core::L2Oracle*>(banks[0].crp);
    AMPERE_ASSERT(!(oracle_crp_ptr && prefetcher_using_oracle), "Oracle driver can only be used by one module");
    if (oracle_crp_ptr) {
        oracle_crp_ptr->oracle_driver.bind(oracle_driver);
        oracle_crp_ptr->real_driver.bind(real_driver);
        oracle_crp_ptr->lsu_write_request = &lsu_write_request;
        oracle_crp_ptr->pwc = &pwc;
        oracle_crp_ptr->memory_stream_name = memory_stream_name.val();
    }

    AMPERE_ASSERT(
        banks.total_fill_buffers() == cbusy.throttle_max,
        "Total fill buffers ({}) and cbusy max requests ({}) do not match: adjust param l2c.cbusy.throttle_max",
        banks.total_fill_buffers(),
        cbusy.throttle_max.val());

    AMPERE_ASSERT(
        !sys_pipe_dealloc || evict_at_fill,
        "Cannot enable scheduling DEALLOC on sys pipe if DEALLOC reads or writes array (enable evict_at_fill)");
}

void L2C::end_of_elaboration()
{
    if (!enable_warmup_events) {
        if (!memory_stream_name.val().empty())
            ms_warmstart(memory_stream_name);
        else if (!fast_warmup_ms.val().empty())
            config->register_warmup(0, [this]() { return gen_warmup(); });
    }
}

void L2C::start_of_simulation()
{
    if (enable_warmup_events) {
        get_environment()->event_stream->enable();
        if (!memory_stream_name.val().empty())
            ms_warmstart(memory_stream_name);
    }
}

bool L2C::run()
{
    receive_from_core();

    send_to_sys();
    receive_from_sys();
    notify_lsu_of_pending_store_types();

    arbitrate();
    read_pipe.run();
    write_pipe.run();
    sys_pipe.run();

    if (enable_per_cycle_pmu) {
        unsigned outstanding_l2_reqs = fb.outstanding_reqs(mesh::chi::RequestOpcode::ReadUnique)
                                       + fb.outstanding_reqs(mesh::chi::RequestOpcode::ReadShared)
                                       + fb.outstanding_reqs(mesh::chi::RequestOpcode::ReadNotSharedDirty);
        for (unsigned i = 0; i < outstanding_l2_reqs; i++) {
            // This PMU only counts non-dataless FB reqs
            event.pmu(event.pmu_num(archx::PMU::L2C_MISS_PERCYC));
        }
    }

    return true; // FIXME: return only when busy
}

l2::Op L2C::memreq_to_op(MemReq req, bool include_atomics)
{
    int bank = banks.select(req->phys_addr);

    switch (req->type) {
    case MemOp::Type::READ:
        return make_clone<l2::Op>(req, l2::Opcode::BLKRD, bank, req);
    case MemOp::Type::READUNIQUE:
        return make_clone<l2::Op>(req, l2::Opcode::BLKRDUNIQUE, bank, req);
    case MemOp::Type::WRITE:
        return make_clone<l2::Op>(
            req,
            (req->atomic_type == MemOp::ATOMIC_LOAD || req->atomic_type == MemOp::ATOMIC_LOAD_WIDE) && include_atomics
                ? l2::Opcode::ATOMIC_LOAD
                : (req->atomic_type == MemOp::ATOMIC_STORE && include_atomics ? l2::Opcode::ATOMIC_STORE
                                                                              : l2::Opcode::SUBWR),
            bank,
            req,
            util::mask(PhysAddr::LINE_SIZE));
    default:
        AMPERE_FATAL("unsupported memory request type {}", req->type);
        return {};
    }
}

void L2C::receive_from_core()
{
    while (immu_request->can_take())
        immu_prq.insert(memreq_to_op(immu_request->take()));

    while (dmmu_request->can_take())
        dmmu_prq.insert(memreq_to_op(dmmu_request->take()));

    while (prefetch_request->can_take()) {
        auto req = prefetch_request->take();
        prefetch_prq.insert(memreq_to_op(req));
    }

    while (icf_request->can_take()) {
        auto [req, l1_contents] = icf_request->take();
        update_inclusion_bits(req, l1_contents, true /* i-side */);
        icf_prq.insert(memreq_to_op(req));
    }

    while (lsu_read_request->can_take()) {
        auto [req, l1_contents] = lsu_read_request->take();
        update_inclusion_bits(req, l1_contents, false /* d-side */);
        lsu_read_prq.insert(memreq_to_op(req));
    }

    while (lsu_evict_request->can_take()) {
        auto req = lsu_evict_request->take();
        lsu_evict_prq.insert(memreq_to_op(req));
    }

    while (lsu_write_request->can_take()) {
        if (pwc.try_combine(lsu_write_request->peek())) {
            lsu_write_request->take();
        } else if (!pwc.full()) {
            auto req = lsu_write_request->take();
            pwc.allocate(req, banks.select(req->phys_addr));
        } else {
            break;
        }
    }

    while (lsu_prefetch_request->can_take()) {
        auto req = lsu_prefetch_request->take();
        prefetch_prq.insert(memreq_to_op(req));
    }

    // If the PWC is full and something is on the way from the LSU -- even if it hasn't arrived yet --
    // then the PWC will try to make room for it.
    if (pwc.full() && lsu_write_request->unsafe_size())
        pwc.set_request_is_waiting();
}

void L2C::send_to_core(MemReq req, LineState state)
{
    bool has_value = state != LineState::I;
    AMPERE_ASSERT(req->origin == MemOp::LSU || has_value,
                  "Only LSU should get speculative responses with invalid data");

    auto resp =
        make_child<MemResp>(req, req->id, req->phys_addr, has_value, req->type, req->atomic_type, is_unique(state));

    switch (req->origin) {
    case MemOp::ICF:
        icf_response->put(resp);
        break;
    case MemOp::LSU:
        lsu_read_response->put(resp);
        break;
    case MemOp::IMMU:
        immu_response->put(resp);
        break;
    case MemOp::DMMU:
        dmmu_response->put(resp);
        break;
    case MemOp::PREFETCH:
    case MemOp::LSU_PREFETCH:
        // We bring prefetches into L2
        break;
    default:
        AMPERE_ERROR("unexpected requester type {}", std::to_underlying(req->origin));
        break;
    }
}

static l2::LineState merge_state(l2::LineState fb_state, mesh::chi::ResponseStatus rsp)
{
    if (is_dirty(fb_state) || rsp == RespComp_UD_PD || rsp == RespComp_SD_PD || rsp == RespWriteData_UD_PD
        || rsp == RespWriteData_SD_PD)
    {
        if (is_unique(fb_state) || rsp == RespComp_UD_PD || rsp == RespWriteData_UD_PD)
            return l2::LineState::UD;
        return l2::LineState::SD;
    } else {
        if (is_unique(fb_state) || rsp == RespComp_UC || rsp == RespWriteData_UC)
            return l2::LineState::UC;
        return l2::LineState::SC;
    }
}

void L2C::receive_from_sys()
{
    if (mesh_port.rx().rsp[0]->can_take()) {
        auto rsp = mesh_port.rx().rsp[0]->take();
        ev_recv_rsp(rsp, rsp->Opcode, evd_txnid(rsp->TxnID), evd_dbid(rsp->DBID), rsp->CBusyS);

        auto& fbe = fb[rsp->TxnID];
        auto& sre = fbe.srq;

        sre.tgt = rsp->SrcID;
        sre.dbid = rsp->DBID;

        cbusy.update(rsp->CBusyS);

        switch (rsp->Opcode) {
        case mesh::chi::ResponseOpcode::RespSepData:
            fbe.line.state = merge_state(fbe.line.state, rsp->Resp);
            sre.comp_rcvd = true;

            if (fbe.all_chunks_returned()) {
                int bank = banks.select(sre.addr);
                fb.srt_deallocate(fbe);
                if (use_fill_first_flit)
                    srq_fill.insert(make_clone<l2::Op>(rsp, l2::Opcode::FILL_FIRSTFLIT, bank, fbe));

                srq_fill.insert(make_clone<l2::Op>(rsp, fbe.fill_opcode(), bank, fbe));
            }

            if (early_comp_ack_enable && sre.need_comp_ack && !sre.sent_comp_ack) {
                auto comp_ack = make_clone<mesh::MeshRsp>(
                    rsp, node_id, fbe.srq.tgt, fbe.srq.dbid, mesh::chi::ResponseOpcode::CompAck);
                sys_rsp_queue.push(comp_ack);
                sre.sent_comp_ack = true;
            }

            break;
        case mesh::chi::ResponseOpcode::Comp:
            fbe.line.state = merge_state(fbe.line.state, rsp->Resp);
            sre.comp_rcvd = true;
            fb.srt_deallocate(fbe);

            if (fbe.srq.chi_request == mesh::chi::RequestOpcode::MakeUnique
                || fbe.srq.chi_request == mesh::chi::RequestOpcode::CleanUnique)
                srq_fill.insert(make_clone<l2::Op>(rsp, fbe.fill_opcode(), banks.select(sre.addr), fbe));
            else
                srq_dealloc.insert(make_clone<l2::Op>(rsp, l2::Opcode::FB_DEALLOC, banks.select(sre.addr), fbe));
            break;
        case mesh::chi::ResponseOpcode::CompDBIDResp:
            // A WriteNoSnp will not fill the cache, so we're done with the FB once we get the DBID
            if (fbe.srq.chi_request == mesh::chi::RequestOpcode::WriteNoSnpFull
                || fbe.srq.chi_request == mesh::chi::RequestOpcode::WriteNoSnpPtl)
            {
                release_credit(fbe.credits.fill_address_credit);
                release_credit(fbe.credits.write_data_credit);
                release_credit(fbe.credits.split_write_data_credit);
            }
            fbe.srq.comp_rcvd = true;
            srq_dealloc.insert(make_clone<l2::Op>(rsp, l2::Opcode::READ_DEALLOC, banks.select(sre.addr), fbe));
            break;
        case mesh::chi::ResponseOpcode::DBIDResp:
            srq_dealloc.insert(make_clone<l2::Op>(rsp, l2::Opcode::READ_VICTIM, banks.select(sre.addr), fbe));
            break;
        case mesh::chi::ResponseOpcode::RetryAck:
            fb.srt_deallocate(fbe);
            retry.push_retry(rsp->PCrdType, rsp->SrcID, rsp->TxnID);
            break;
        case mesh::chi::ResponseOpcode::PCrdGrant: {
            auto& retry_fbe = fb[retry.pop_retry(rsp->PCrdType, rsp->SrcID)];
            auto retry_req = make_clone<mesh::MeshReq>(
                rsp, node_id, rsp->SrcID, rsp->TxnID, retry_fbe.srq.chi_request, retry_fbe.srq.addr);
            retry_req->AllowRetry = false;
            retry_req->PCrdType = rsp->PCrdType;
            sys_req_queue.push(retry_req);
            break;
        }
        default:
            AMPERE_ERROR("unimplemented CHI response opcode {}", rsp->Opcode);
            break;
        }
    }

    if (mesh_port.rx().dat[0]->can_take()) {
        auto dat = mesh_port.rx().dat[0]->take();

        ev_recv_dat(dat, dat->Opcode, evd_txnid(dat->TxnID), evd_dbid(dat->DBID), dat->CBusyS);

        cbusy.update(dat->CBusyS);

        auto& fbe = fb[dat->TxnID];
        auto& sre = fbe.srq;
        int bank = banks.select(sre.addr);

        switch (dat->Opcode) {
        case mesh::chi::DataOpcode::CompData:
            fbe.line.state = merge_state(fbe.line.state, dat->Resp);
            sre.comp_rcvd = true;
            sre.tgt = dat->HomeNID;
            sre.dbid = dat->DBID;
            break;
        case mesh::chi::DataOpcode::CopyBackWrData:
            // data received by core as part of the stash flow: fill this copyback write data in L3
            fbe.line.state = merge_state(fbe.line.state, dat->Resp);
            sre.comp_rcvd = true;
            break;
        case mesh::chi::DataOpcode::DataSepResp:
            // if (early_comp_ack_enable && sre.need_comp_ack && !sre.sent_comp_ack) {
            //     auto comp_ack = make_clone<mesh::MeshRsp>(dat, node_id, fbe.srq.tgt, fbe.srq.dbid,
            //     mesh::chi::ResponseOpcode::CompAck); sys_rsp_queue.push(comp_ack); sre.sent_comp_ack = true;
            // }
            break;
        default:
            AMPERE_ERROR("unimplemented CHI data opcode {}", dat->Opcode);
            break;
        }

        fbe.merge_chunks_returned(data_id_to_chunks(dat->DataID));

        if (sre.comp_rcvd && fbe.all_chunks_returned()) {
            // Read requests come back with data/response combined.
            fb.srt_deallocate(fbe);
            if (use_fill_first_flit)
                srq_fill.insert(make_clone<l2::Op>(dat, l2::Opcode::FILL_FIRSTFLIT, bank, fbe));

            srq_fill.insert(make_clone<l2::Op>(dat, fbe.fill_opcode(), bank, fbe));
        }
    }

    if (mesh_port.rx().snp[0]->can_take()) {
        auto snp = mesh_port.rx().snp[0]->take();
        ev_recv_snp(snp, snp->Opcode, snp->Addr, evd_txnid(snp->TxnID));
        switch (snp->Opcode) {
        case mesh::chi::SnoopOpcode::SnpStashShared:
            event.recv_stash(snp, snp->Opcode, snp->Addr, evd_txnid(snp->TxnID));
            // Allocate a way in L3
            snpq.insert(make_clone<l2::Op>(snp, l2::Opcode::ALLOC_L3, banks.select(snp->Addr), snp));
            break;
        case mesh::chi::SnoopOpcode::SnpUnique:
        case mesh::chi::SnoopOpcode::SnpCleanInvalid:
        case mesh::chi::SnoopOpcode::SnpMakeInvalid:
            snpq.insert(make_clone<l2::Op>(snp, l2::Opcode::SNP_INV, banks.select(snp->Addr), snp));
            break;
        case mesh::chi::SnoopOpcode::SnpUniqueFwd:
        case mesh::chi::SnoopOpcode::SnpCleanFwd:
            snpq.insert(make_clone<l2::Op>(snp, l2::Opcode::SNP_FWD, banks.select(snp->Addr), snp));
            break;
        default:
            AMPERE_FATAL("unimplemented CHI snoop opcode {}", snp->Opcode);
            break;
        }
    }
}

void L2C::send_to_sys()
{
    while (!sys_rsp_queue.empty() && mesh_port.tx().rsp[0]->can_put()) {
        auto rsp = sys_rsp_queue.front();
        sys_rsp_queue.pop();

        mesh_port.tx().rsp[0]->put(rsp);
        ev_send_rsp(rsp, rsp->Opcode, evd_srcid(rsp->SrcID.id()), evd_tgtid(rsp->TgtID.id()), evd_txnid(rsp->TxnID));
    }

    while (!sys_dat_queue.empty() && mesh_port.tx().dat[0]->can_put()) {
        auto dat = sys_dat_queue.front();
        sys_dat_queue.pop();

        mesh_port.tx().dat[0]->put(dat);
        ev_send_dat(dat, dat->Opcode, evd_srcid(dat->SrcID.id()), evd_tgtid(dat->TgtID.id()), evd_txnid(dat->TxnID));
    }

    if (!sys_req_queue.empty() && !mesh_port.tx().req[0]->can_put())
        event.mesh_backup(sys_req_queue.front());

    while (!sys_req_queue.empty() && mesh_port.tx().req[0]->can_put() && fb.srt_count() < cbusy.num_requests()) {
        auto req = sys_req_queue.front();
        sys_req_queue.pop();

        fb.srt_allocate(req->TxnID);
        mesh_port.tx().req[0]->put(req);
        ev_send_req(
            req, req->Opcode, req->Addr, evd_srcid(req->SrcID.id()), evd_tgtid(req->TgtID.id()), evd_txnid(req->TxnID));

        AMPERE_ASSERT(fb[req->TxnID].state != l2::FillBufferEntry::State::INVALID,
                      "sending request with invalid FB[{}]",
                      req->TxnID);

        event.pmu(req, event.pmu_num(archx::PMU::BUS_ACCESS));
        if (is_read(req->Opcode))
            event.pmu(req, event.pmu_num(archx::PMU::BUS_ACCESS_RD));
        if (is_write(req->Opcode))
            event.pmu(req, event.pmu_num(archx::PMU::BUS_ACCESS_WR));
    }
}

void L2C::notify_lsu_of_pending_store_types()
{
    unsigned new_pending_bank_store_types = in_flight_store_types() | fb.store_types();
    unsigned new_pending_store_types = pwc.store_types() | new_pending_bank_store_types;

    auto fifo_stores = lsu_write_request->unsafe_size();
    for (uint64_t i = 0; i < fifo_stores; i++)
        new_pending_store_types |= lsu_write_request->unsafe_peek(i)->store_types();

    unsigned changes = new_pending_store_types ^ pending_store_types;

    if (changes) {
        if (changes & StoreType::STORE_ANY) {
            // l2_barrier_out gets TRUE if there are no more stores in the system (telling the LSU a barrier can
            // proceed)
            l2_no_pending_stores_out->put((new_pending_store_types & StoreType::STORE_ANY) == 0);
        }

        if (changes & StoreType::STORE_BARRIER)
            l2_barrier_out->put((new_pending_store_types & StoreType::STORE_BARRIER) != 0);

        if (changes & StoreType::STORE_RELEASE)
            l2_store_release_out->put((new_pending_store_types & StoreType::STORE_RELEASE) != 0);

        if (changes & StoreType::STORE_EXCLUSIVE)
            l2_store_exclusive_out->put((new_pending_store_types & StoreType::STORE_EXCLUSIVE) != 0);

        pending_store_types = new_pending_store_types;
    }

    pwc.set_pending_writes((new_pending_bank_store_types & StoreType::STORE_ANY) != 0);

    while (lsu_has_barrier_in->can_take())
        pwc.set_pending_barrier(lsu_has_barrier_in->take());
}

mesh::MeshReq L2C::gen_sys_req(
    const l2::Op& op, l2::FillBufferEntry& fbe, mesh::RequestOpcode opcode, PhysAddr addr, bool likely_shared)
{
    fbe.reset_srq();

    fbe.srq.chi_request = opcode;
    fbe.srq.tgt = sam->lookup(addr);
    fbe.srq.addr = addr;
    fbe.srq.dbid = 0;

    auto req = make_child<mesh::MeshReq>(op, node_id, fbe.srq.tgt, fbe.id, opcode, addr);
    req->LikelyShared = likely_shared;
    req->RSVDC =
        fbe.line.cache_type == CacheType::L3 ? ReserveCustomerUseReq(RsvdcReq::CopyBackL2L3) : ReserveCustomerUseReq();
    fbe.srq.need_comp_ack = req->ExpCompAck;

    return req;
}

mesh::MeshReq L2C::gen_sys_req(const l2::Op& op, mesh::RequestOpcode opcode, PhysAddr addr, bool likely_shared)
{
    auto& fbe = fb[op->fbid];

    return gen_sys_req(op, fbe, opcode, addr, likely_shared);
}

l2::FillBufferEntry& L2C::fb_allocate(l2::Op& op, bool snp, bool eviction)
{
    FBCredits remainder_credits;
    if (parallel_eviction) {
        if (eviction) {
            std::swap(remainder_credits.fill_address_credit, op->fb_credits.fill_address_credit);
            std::swap(remainder_credits.write_data_credit, op->fb_credits.write_data_credit);
            std::swap(remainder_credits.split_write_data_credit, op->fb_credits.split_write_data_credit);
            std::swap(remainder_credits.unified_credit, op->fb_credits.unified_credit);
        } else {
            std::swap(remainder_credits.eviction_address_credit, op->fb_credits.eviction_address_credit);
            std::swap(remainder_credits.eviction_data_credit, op->fb_credits.eviction_data_credit);
            std::swap(remainder_credits.split_eviction_data_credit, op->fb_credits.split_eviction_data_credit);
        }
    }
    auto& fbe = fb.allocate(std::move(op->fb_credits));
    std::swap(op->fb_credits, remainder_credits);
    fbe.line.addr = op->addr;
    fbe.bank_id = banks.select(op->addr);
    if (snp)
        fbe.allocated_by_snp = op->snp;
    else
        fbe.allocated_by_req = op->req;

    if (op->opcode == l2::Opcode::BLKRD || op->opcode == l2::Opcode::BLKRDUNIQUE
        || op->opcode == l2::Opcode::ATOMIC_LOAD || op->opcode == l2::Opcode::ATOMIC_CAS)
        fbe.add_response(op->req);

    if (op->opcode == l2::Opcode::ATOMIC_LOAD || op->opcode == l2::Opcode::ATOMIC_CAS)
        fbe.type = l2::FillBufferEntry::Type::FB_ATOMIC_LOAD;
    else if (op->opcode == l2::Opcode::ATOMIC_STORE)
        fbe.type = l2::FillBufferEntry::Type::FB_ATOMIC_STORE;

    op->fbid = fbe.id;

    event.fb_alloc(op, event.fbid(op->fbid), event.phys_line(op->addr.line_address().value()));

    fb_used[fbe.bank_id]++;
    (*event.fb_used[fbe.bank_id])(event.occupancy(fb_used[fbe.bank_id]));

    return fbe;
}

void L2C::fb_deallocate(l2::Op& op)
{
    AMPERE_ASSERT(op->has_fb(), "op has no fill buffer entry to deallocate");

    auto& fbe = fb[op->fbid];

    AMPERE_ASSERT(fbe.credits, "fill buffer entry has no credit");

    if (!fbe.credits.complete())
        return;

    if (fbe.allocated_by_req) {
        event.fb_dealloc(fbe.allocated_by_req, event.fbid(op->fbid), event.phys_line(op->addr.line_address().value()));
        event.fb_dealloc_req(fbe.allocated_by_req);
    } else if (fbe.allocated_by_snp) {
        event.fb_dealloc(fbe.allocated_by_snp, event.fbid(op->fbid), event.phys_line(op->addr.line_address().value()));
        event.fb_dealloc_stash(fbe.allocated_by_snp);
    }

    AMPERE_ASSERT(!fbe.srq.allocated, "attempted to deallocate a fb that has an outstanding srq entry");
    fb.deallocate(op->fbid);
    fb_used[op->bank]--;
    (*event.fb_used[op->bank])(event.occupancy(fb_used[op->bank]));
    op->fbid = -1;
}

void L2C::set_va_bits(PhysAddr pa, VirtAddr va)
{
    if (num_va_bits == 0)
        return;

    auto l2_hit = banks[banks.select(pa)].lookup(pa, false);
    auto fb_hit = fb.lookup(pa);

    if (l2_hit)
        l2_hit->va_bits = va.extract_range(12 + num_va_bits - 1, 12);
    if (fb_hit)
        fb_hit->line.va_bits = va.extract_range(12 + num_va_bits - 1, 12);
}

void L2C::set_inclusion_bit(PhysAddr addr, bool value, bool iside)
{
    auto l2_hit = banks[banks.select(addr)].lookup(addr, false);

    if (l2_hit) {
        if (iside)
            l2_hit->in_l1i = value;
        else
            l2_hit->in_l1d = value;
    }

    fb.set_inclusion_bit(addr, value, iside);
}

bool L2C::get_l1i(const l2::Line& line) const
{
    if (perfect_l1i_bits)
        return l1i_lookup->address_resident(line.addr);
    return line.in_l1i;
}

bool L2C::get_l1d(const l2::Line& line) const
{
    if (perfect_l1d_bits)
        return l1d_lookup->address_resident(line.addr);
    return line.in_l1d;
}

bool L2C::perfect_hit(VirtAddr addr) const
{
    // VA[52] is between the upper end of the VA space (even with 64k pages, which allow 52-bit VAs)
    // and below any bits affected by TBI/MTE, so it's the reliable bit to look at to determine
    // whether an address goes through TTBR0 or TTBR1.
    return enable_perfect_mode || (enable_perfect_mode_user && addr[52] == 0);
}

void L2C::check_inclusion_bit(PhysAddr addr, bool iside)
{
    auto l2_hit = banks[banks.select(addr)].lookup(addr.line_address(), false);
    auto fb_hit = fb.check_inclusion_bit(addr.line_address(), iside);

    if (iside && !perfect_l1i_bits && !enable_perfect_mode && !enable_perfect_mode_user && !never_fill) {
        AMPERE_ASSERT(l2_hit || fb_hit, "Line {} in IL1 not in L2 or FB", addr);
        if (l2_hit)
            AMPERE_ASSERT(get_l1i(*l2_hit), "Line {} in IL1 is in L2 but does not have l1i bit set", addr);
        else if (fb_hit)
            AMPERE_ASSERT(*fb_hit, "Line {} in IL1 is in FB but does not have l1i bit set", addr);
    } else if (!iside && !perfect_l1d_bits && !enable_perfect_mode && !enable_perfect_mode_user && !never_fill) {
        AMPERE_ASSERT(l2_hit || fb_hit, "Line {} in DL1 not in L2 or FB", addr);
        if (l2_hit)
            AMPERE_ASSERT(get_l1d(*l2_hit), "Line {} in DL1 is in L2 but does not have l1d bit set", addr);
        else if (fb_hit)
            AMPERE_ASSERT(*fb_hit, "Line {} in DL1 is in FB but does not have l1d bit set", addr);
    }
}

// In real life, the L1 sends a vector of which ways of req's L2 set are still present in the L1.
// Here, l1_contents is the list of all the physical addresses which are in the L1 and might map
// to req's L2 set (everything from the L1 set(s) which map to that L2 set). Addresses not in the
// req's L2 set are used only for coherency checking; addresses which are in req's L2 set are used
// to update the L1-present bits for S.
void L2C::update_inclusion_bits(MemReq& req, uarch_common::Request::L1Contents& l1_contents, bool iside)
{
    // First check that all the lines reported by the L1 have their L1 bit set
    for (auto addr : l1_contents.set_addrs)
        check_inclusion_bit(addr, iside);

    // For Trinity's 32K I-cache, we disabled I-bit updates from ICF
    if (never_clear_l1i && iside)
        return;

    // Trinity also stopped clearing D-bits so the L2-way storage could be removed from DL1 tags
    if (never_clear_l1d && !iside)
        return;

    auto banknum = banks.select(req->phys_addr);
    auto index = banks[banknum].compute_set(req->phys_addr);

    // If num_va_bits is 0, set_va_bits() always sets the value in the line to 0, and we also get 0 here,
    // so the VA bit value always matches -- as if there is no VA tagging at all.
    uint64_t va_bits{0};
    if (num_va_bits)
        va_bits = req->virt_addr.extract_range(12 + num_va_bits - 1, 12);

    // Reset all the L1-present bits in the L2 set which is the target of the new request
    if (!l1_contents.no_clear_l1_bits) {
        banks[banknum].clear_l1_bits_in_set(index, va_bits, iside);
    }

    // Now set the L1-present bit for any line which is still in the L1 and is in the same L2 set as the request
    if (iside) {
        for (auto addr : l1_contents.set_addrs)
            if (banks.select(addr) == banknum && banks[banknum].compute_set(addr) == index)
                set_inclusion_bit(addr, true, iside);
    } else {
        for (auto addr : l1_contents.set_addrs) {
            if (banks.select(addr) == banknum && banks[banknum].compute_set(addr) == index) {
                auto l2_hit = banks[banks.select(addr)].lookup(addr, false);
                auto fb_hit = fb.lookup(addr);
                if ((l2_hit && l2_hit->va_bits == va_bits) || (fb_hit && fb_hit->line.va_bits == va_bits))
                    set_inclusion_bit(addr, true, iside);
            }
        }
    }
}

boost::optional<l2::Line&> L2C::lookup_l2(const l2::Op& op)
{
    bool is_prefetch = op->req->origin == MemOp::Requester::PREFETCH;
    bool is_iside = ((op->req->origin == MemOp::Requester::ICF) || (op->req->origin == MemOp::Requester::IMMU));
    bool is_dside = ((op->req->origin == MemOp::Requester::LSU) || (op->req->origin == MemOp::Requester::DMMU));

    if (!is_prefetch) {
        event.pmu(op->req, event.pmu_num(archx::PMU::L2D_CACHE));

        if (op->req->type == MemOp::Type::WRITE)
            event.pmu(op->req, event.pmu_num(archx::PMU::L2D_CACHE_WR));
        else
            event.pmu(op->req, event.pmu_num(archx::PMU::L2D_CACHE_RD));

        if (is_iside) {
            event.pmu(op->req, event.pmu_num(archx::PMU::L2I_CACHE));
            event.pmu(op->req, event.pmu_num(archx::PMU::L2C_CACHE_INST));
        }
    }

    if (perfect_hit(op->req->virt_addr) || (mmu_always_hit && op->req->origin == MemOp::Requester::DMMU)) {
        static l2::Line perfect_line;
        perfect_line.addr = op->addr;
        perfect_line.state = l2::LineState::UC;
        perfect_line.cache_type = CacheType::L2;
        event.access(op, event.hit_data(true), op->addr);
        return perfect_line;
    } else {
        boost::optional<Line&> l2_hit;
        if (!enable_antiperfect_mode)
            l2_hit = banks[op->bank].lookup(op->addr, !is_prefetch, op->req);

        if (!l2_hit) {
            if (!is_prefetch) {
                if (op->req->type != MemOp::Type::WRITE) {
                    l2_miss_out->put(op->req->phys_addr);
                    l2_miss_out_to_icf->put(op->req->phys_addr);
                }
                if (is_dside)
                    event.pmu(op->req, event.pmu_num(archx::PMU::L2D_CACHE_LMISS_RD));
                if (is_iside)
                    event.pmu(op->req, event.pmu_num(archx::PMU::L2I_CACHE_LMISS));
            }
            event.access(op, event.hit_data(false), op->addr);
        } else {
            AMPERE_ASSERT(!never_fill, "Somehow we got a hit in never_fill mode");
            event.pmu(op->req, event.pmu_num(archx::PMU::L2C_CACHE_HIT));
            if (!is_prefetch) {
                event.pmu(op->req, event.pmu_num(archx::PMU::L2C_CACHE_HIT_DEMAND));
            }
            event.access(op, event.hit_data(true), op->addr);
            if ((*l2_hit).cache_type == CacheType::L3)
                event.hit_l3(op, op->addr);
        }

        return l2_hit;
    }
}

void L2C::preload(PhysAddr addr, l2::LineState state)
{
    l2::Line line{addr, state, CacheType::L2};
    banks[banks.select(addr)].preload_in_l2(line, nullptr);
}

void L2C::preload_l3(PhysAddr addr, l2::LineState state)
{
    l2::Line line{addr, state, CacheType::L3};
    banks[banks.select(addr)].preload_in_l3(line);
}

void L2C::preload_with_inclusion_bits(PhysAddr addr, l2::LineState state, MemOp::Requester requester)
{
    l2::Line line{addr, state, CacheType::L2};
    if (requester == MemOp::Requester::LSU)
        line.in_l1d = true;
    else
        line.in_l1i = true;
    banks[banks.select(addr)].preload_in_l2(line, nullptr);
}

l2::LineState L2C::peek_state(PhysAddr addr)
{
    auto line = banks[banks.select(addr)].lookup(addr, false);
    return line ? line->state : l2::LineState::I;
}

bool L2C::is_l1d_set(PhysAddr addr)
{
    auto line = banks[banks.select(addr)].lookup(addr, false);
    return line->in_l1d;
}

bool L2C::is_l1i_set(PhysAddr addr)
{
    auto line = banks[banks.select(addr)].lookup(addr, false);
    return line->in_l1i;
}

mesh::chi::FinalState L2C::l2_state_to_chi(l2::LineState state)
{
    switch (state) {
    case l2::LineState::I:
        return mesh::chi::FinalState::I;
    case l2::LineState::SC:
        return mesh::chi::FinalState::SC;
    case l2::LineState::UC:
        return mesh::chi::FinalState::UC;
    case l2::LineState::SD:
        return mesh::chi::FinalState::SD;
    case l2::LineState::UD:
        return mesh::chi::FinalState::UD;
    default:
        AMPERE_ERROR("Unexpected L2 line state {}", state);
        return mesh::chi::FinalState::I;
    }
}

std::pair<PhysAddr, mesh::chi::FinalState> L2C::warmstart_access(ms::Access ms_access, bool from_core)
{
    AMPERE_ASSERT(memory_stream_name.val().empty() || !from_core,
                  "SOC unified warmup and L2C warmup cannot both be used");
    warmup_access_count++;

    auto addr = PhysAddr(ms_access.pa()).line_address();
    l2::LineState state = (ms::Access_Type_WRITE == ms_access.type()) ? l2::LineState::UD : l2::LineState::UC;

    l2::Line line{addr, state, CacheType::L2};
    int bank = banks.select(addr);

    // For now, only send in the PC on data accesses since ICF doesn't send it
    auto req =
        make<MemReq>(0,
                     addr,
                     (ms::Access_Source_DATA == ms_access.source()) ? MemOp::Requester::LSU : MemOp::Requester::ICF,
                     (ms::Access_Type_WRITE == ms_access.type()) ? MemOp::Type::WRITE : MemOp::Type::READ,
                     ms_access.size(),
                     false /* nontemporal */,
                     MemOp::PrefetchType::NONE,
                     MemOp::BarrierType::BAR_NONE,
                     MemOp::ATOMIC_NONE,
                     MemOp::SNOOP_NONE,
                     PageSize::Size::INVALID,
                     0 /* bytemask */,
                     0x0_va,
                     (ms_access.source() == ms::Access_Source_DATA) ? ProgramCounter{ms_access.pc()} : 0x0_pc);

    if (check_warmstart_overruns && banks[bank].lookup(addr, false))
        AMPERE_MODULE_THROW("L2C preload hit previously preloaded address");

    // Preload L2 -- returns victim on miss, or null on hit
    auto victim = banks[bank].preload_in_l2(line, req);
    bool evicted = victim && victim->state != l2::LineState::I;

    // Preload prefetcher
    if (warmup_prefetcher) {
        uint64_t set_index = L2C::index(addr);
        prefetcher->fill(set_index, req, false, false);
        if (evicted)
            prefetcher->evict(addr, false, true);
    }

    if (victim) {
        auto [_, new_line] = warmup_lines.insert(addr);
        if (evicted) {
            if (check_warmstart_overruns)
                AMPERE_MODULE_THROW("Unexpectedly evicted valid line during L2C preload");

            event.preload_evict();
            warmup_evicts++;
            if (!new_line) {
                event.warmup_fill_again();
                warmup_fill_again++;
            }

            return {victim->addr, l2_state_to_chi(victim->state)};
        } else {
            event.preload_fill_invalid();
            warmup_fill_invalid++;
            AMPERE_ASSERT(new_line, "Second fill of a line went to an invalid way");
        }
    } else { // hit
        AMPERE_ASSERT(warmup_lines.contains(addr), "Hit on a line which was not previously seen");
        event.preload_hit();
        warmup_hits++;
    }

    return {0x0_pa, mesh::chi::FinalState::I};
}

// read in memory stream file and warm up just the L2C
void L2C::ms_warmstart(std::string filename)
{
    ms::MemoryStreamReader ms_reader(filename.c_str());
    ms::Access ms_access;
    ms::Event ms_event;

    AMPERE_ASSERT(!never_fill, "Don't preload the L2 when never_fill mode is enabled");

    fmt::print(std::cout, "Warmstart: L2C with file {}\n", filename);

    while (true) {
        if (1 == ms_reader.version()) {
            if (!ms_reader.ProtobufStreamReader::read(ms_access))
                break;
        } else {
            if (!ms_reader.read(ms_event))
                break;
            if (ms_event.has_control()) {
                const auto& control = ms_event.control();
                // v2 memory streams filter through a 2MB cache, so we want to use the cache dump at the end to preload
                // the L2. But v3 streams filter through L1-sized caches, so the L2 has already seen the correct traffic
                if ((warmup_stream_only || ms_reader.version() == 3) && control.type() == ms::Control_Type_END_MEMORY)
                    break;
                continue;
            }

            AMPERE_ASSERT(ms_event.has_access(), "Memory event has no access");
            ms_access.Swap(ms_event.mutable_access());
        }

        warmstart_access(ms_access, false);

        ms_access.Clear();
    }

    // RTL warmstart doesn't initialize the LRU/RRIP state, so reset it to match
    if (reset_lru_after_warmup)
        banks[0].crp->init(num_banks, banks[0].get_num_sets(), banks[0].get_num_ways(), banks[0].ways_mask);

    warmstart_complete();
}

void L2C::warmstart_complete()
{
    fmt::print(std::cout,
               "Warmstart: L2C completed {} transactions with {} unique lines: {} fills to invalid way, {} hits, {} "
               "evicts, {} duplicate fills\n",
               warmup_access_count,
               warmup_lines.size(),
               warmup_fill_invalid,
               warmup_hits,
               warmup_evicts,
               warmup_fill_again);

    warmup_lines.clear();
}

std::optional<soc::WarmupInfo> L2C::gen_warmup()
{
    PhysAddr address;
    soc::WarmupInfo::Action action;

    auto reader = ms_warmup.ms_reader.get();
    if (!reader->read(ms_warmup.ms_access))
        return {};
    address = PhysAddr(ms_warmup.ms_access.pa()) + phyaddroffset;
    if (ms::Access_Type_WRITE == ms_warmup.ms_access.type())
        action = soc::WarmupInfo::WRITE;
    else
        action = soc::WarmupInfo::READ;

    // clear out to read next transaction
    ms_warmup.ms_access.Clear();
    return soc::WarmupInfo{address, action, node_id};
}

void L2C::pmu_refill_events(l2::Op& op)
{
    switch (op->req->origin) {
    case MemOp::Requester::PREFETCH:
        event.pmu(op, event.pmu_num(archx::PMU::L2_PREFETCH_REFILL));
        break;
    case MemOp::Requester::IMMU:
    case MemOp::Requester::ICF:
        event.pmu(op, event.pmu_num(archx::PMU::L2D_CACHE_REFILL));
        event.pmu(op, event.pmu_num(archx::PMU::L2C_INST_REFILL));
        event.pmu(op, event.pmu_num(archx::PMU::L2D_CACHE_REFILL_RD));
        break;
    case MemOp::Requester::DMMU:
    case MemOp::Requester::LSU:
    case MemOp::Requester::LSU_PREFETCH:
        event.pmu(op, event.pmu_num(archx::PMU::L2D_CACHE_REFILL));
        event.pmu(op, event.pmu_num(archx::PMU::L2C_DATA_REFILL));
        if (op->opcode == l2::Opcode::SUBWR && (*op->chi.req)->Opcode == mesh::chi::RequestOpcode::ReadUnique)
            event.pmu(op, event.pmu_num(archx::PMU::L2D_CACHE_REFILL_WR));
        else
            event.pmu(op, event.pmu_num(archx::PMU::L2D_CACHE_REFILL_RD));
        break;
    default:
        break;
    }
}

std::uint8_t L2C::data_id_to_chunks(uint8_t data_id)
{
    switch (config->data_bus_width()) {
    case 512:
        return 0b1111;
    case 256:
        return 0b11 << data_id;
    case 128:
        return 0b01 << data_id;
    default:
        AMPERE_ERROR("unsupported data bus width {}", config->data_bus_width());
        return 0;
    }
}

Index L2C::index(PhysAddr addr) const
{
    auto bank_num = banks.select(addr);
    auto set_bits = banks[bank_num].get_set_bits();
    return (bank_num << set_bits) | banks[bank_num].compute_set(addr);
}

bool L2C::line_present(const PhysAddr& addr, bool iside)
{
    auto l2_hit = banks[banks.select(addr)].lookup(addr, false);
    auto fb_hit = fb.lookup(addr);

    if (l2_hit && ((iside && get_l1i(*l2_hit)) || (!iside && get_l1d(*l2_hit))))
        return true;
    if (fb_hit && ((iside && get_l1i(fb_hit->line)) || (!iside && get_l1d(fb_hit->line))))
        return true;

    if (!l2_hit && !fb_hit)
        AMPERE_ERROR("L2 inclusion error: line PA {} in L1 not present in L2 or fill buffers, iside {}", addr, iside);
    else if (l2_hit)
        AMPERE_ERROR("L2 inclusion error: line PA {} in L1 is in L2 but does not have the L1I or L1D bit set, iside {}",
                     addr,
                     iside);
    else if (fb_hit)
        AMPERE_ERROR(
            "L2 inclusion error: line PA {} in L1 is in fill buffers but does not have the L1I or L1D bit set, iside "
            "{}",
            addr,
            iside);
    else
        AMPERE_ERROR("Unexpected case in coherency checker");

    return false;
}

void L2C::dump(std::ostream& os) const
{
    // FIXME: add PWC
    cbusy.dump(os);
    snpq.dump(os);
    srq_fill.dump(os);
    srq_dealloc.dump(os);
    icf_prq.dump(os);
    immu_prq.dump(os);
    dmmu_prq.dump(os);
    lsu_read_prq.dump(os);
    prefetch_prq.dump(os);
    fb.dump(os);
}
} // namespace ampere::core
