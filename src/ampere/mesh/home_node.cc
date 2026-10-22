//
// Copyright (c) 2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <signal.h>
#include "ampere/mesh/home_node.h"

namespace ampere::mesh {

HomeNode::HomeNode(const sc_core::sc_module_name& name, sc::Clock* clock) : MeshModule{name, clock}
{
    retry.define_credit_type(0, tracker.size);
    connect(slc_hit_data_completion).name("slc_hit_data_completion").default_bandwidth(4).default_latency(0);
}

bool HomeNode::run()
{
    transmit();
    receive_requests();
    receive_responses();
    main_pipe.run();

    send_delayed_data_to_requester();

    bool empty = tracker.empty() && txreq.empty() && txrsp.empty() && txsnp.empty() && txdat.empty();
    return !empty;
}

void HomeNode::receive_requests()
{
    if (mesh_port.rx().req[0]->can_take()) {
        auto req = mesh_port.rx().req[0]->take();
        ev_recv_req(req, req->Addr, req->Opcode, evd_txnid(req->TxnID));

        auto [can_process, pcrd_type] = retry.process_request(req);

        if (can_process) {
            auto [entry, blocked] = tracker.allocate(req);
            ev_alloc(req, evd_occupancy(tracker.occupancy()));
            tracker.process_event(*entry, tracker_fsm::Initialize{blocked});
        } else {
            if (req->Opcode != RequestOpcode::PrefetchTgt) {
                auto rsp = make_clone<MeshRsp>(req, node_id, req->SrcID, req->TxnID, ResponseOpcode::RetryAck);
                rsp->CBusyS = tracker.get_cbusy();
                rsp->PCrdType = pcrd_type;
                txrsp.push_back(rsp);
            }
        }
    }
}

void HomeNode::receive_responses()
{
    if (mesh_port.rx().rsp[0]->can_take()) {
        auto rsp = mesh_port.rx().rsp[0]->take();
        ev_recv_rsp(rsp, rsp->Opcode, evd_srcid(rsp->SrcID.id()), evd_txnid(rsp->TxnID), rsp->CBusyS);

        uint16_t txn_id =
            ((rsp->Opcode == chi::ResponseOpcode::PCrdGrant) ? retry_requester.pop_retry(rsp->PCrdType, rsp->SrcID)
                                                             : rsp->TxnID);

        auto& entry = tracker.find_by_id(txn_id);

        bool forwarded = false;

        switch (rsp->Opcode) {
        case chi::ResponseOpcode::ReadReceipt:
            rcbusy->update(rsp->CBusyS, outstanding_requests);
            tracker.process_event(entry, tracker_fsm::ReadReceipt{});
            break;
        case chi::ResponseOpcode::SnpRespFwded:
            forwarded = true;
            // fall through
        case chi::ResponseOpcode::SnpResp:
            // LDID not needed here as the SnpResp packet is not altered in IDT
            entry.snoop_status.set_state(rsp->SrcID, rsp->Resp.state(), false);
            tracker.process_event(entry, tracker_fsm::SnoopResponse{forwarded});
            break;
        case chi::ResponseOpcode::CompAck:
            tracker.process_event(entry, tracker_fsm::CompAck{});
            break;
        case chi::ResponseOpcode::CompDBIDResp:
            rcbusy->update(rsp->CBusyS, outstanding_requests);
            tracker.process_event(entry, tracker_fsm::DataReq{rsp->DBID});
            break;
        case chi::ResponseOpcode::RetryAck:
            rcbusy->update(rsp->CBusyS, outstanding_requests);
            tracker.process_event(entry, tracker_fsm::RetryAck{rsp->PCrdType});
            retry_requester.push_retry(rsp->PCrdType, rsp->SrcID, txn_id);
            if (entry.is_mem_request) {
                AMPERE_ASSERT(outstanding_requests > 0, "oustanding_requests droped below 0");
                --outstanding_requests;
            }
            break;
        case chi::ResponseOpcode::PCrdGrant:
            rcbusy->update(rsp->CBusyS, outstanding_requests);
            tracker.process_event(entry, tracker_fsm::CreditGrant{rsp->PCrdType});
            break;
        default:
            AMPERE_ERROR("can't handle response opcode {}", rsp->Opcode);
        }
    }

    if (mesh_port.rx().dat[0]->can_take()) {
        auto dat = mesh_port.rx().dat[0]->take();
        ev_recv_dat(dat, dat->Opcode, evd_txnid(dat->TxnID));

        rcbusy->update(dat->CBusyS, outstanding_requests);

        auto& entry = tracker.find_by_id(dat->TxnID);

        if (dat->Opcode == chi::DataOpcode::SnpRespData) {
            entry.chunk_valid |= data_id_to_chunk(dat->DataID);

            if (entry.chunk_valid == 0xf) {
                // LDID not needed here as the SnpRespData packet is not altered in IDT
                entry.snoop_status.set_state(dat->SrcID, dat->Resp.state(), false);
                tracker.process_event(entry, tracker_fsm::SnoopResponse{false});
            }
        } else if (dat->Opcode == chi::DataOpcode::CompData) {
            if (entry.request->LikelyShared)
                tracker.process_event(entry, tracker_fsm::DataRcvd{data_id_to_chunk(dat->DataID), true, false});
        } else if (dat->Opcode == chi::DataOpcode::CopyBackWrData) {
            tracker.process_event(
                entry, tracker_fsm::DataRcvd{data_id_to_chunk(dat->DataID), enable_slc, dat->Resp.pass_dirty()});
        } else
            AMPERE_ERROR("can't handle data opcode {}", dat->Opcode);
    }
}

void HomeNode::transmit()
{
    if (!txreq.empty() && mesh_port.tx().req[0]->can_put() && outstanding_requests < rcbusy->num_requests()) {
        auto pkt = txreq.front();
        txreq.pop_front();
        mesh_port.tx().req[0]->put(pkt);
        ev_send_req(pkt,
                    pkt->Opcode,
                    pkt->Addr,
                    evd_srcid(pkt->SrcID.id()),
                    evd_tgtid(pkt->TgtID.id()),
                    evd_txnid(pkt->TxnID),
                    evd_outstanding(outstanding_requests));
        ++outstanding_requests;
    }

    transmit_channel<MeshRsp>(txrsp, mesh_port.tx().rsp[0], [this](const MeshRsp& pkt) {
        ev_send_rsp(pkt,
                    pkt->Opcode,
                    evd_srcid(pkt->SrcID.id()),
                    evd_tgtid(pkt->TgtID.id()),
                    evd_txnid(pkt->TxnID),
                    pkt->CBusyS);
    });
    transmit_channel<MeshSnp>(txsnp, mesh_port.tx().snp[0], [this](const MeshSnp& pkt) {
        ev_send_snp(
            pkt, pkt->Opcode, pkt->Addr, evd_srcid(pkt->SrcID.id()), evd_tgtid(pkt->TgtID.id()), evd_txnid(pkt->TxnID));
    });
    transmit_channel<MeshDat>(txdat, mesh_port.tx().dat[0], [this](const MeshDat pkt) {
        ev_send_dat(pkt,
                    pkt->Opcode,
                    evd_srcid(pkt->SrcID.id()),
                    evd_tgtid(pkt->TgtID.id()),
                    evd_txnid(pkt->TxnID),
                    pkt->CBusyS);
    });
}

template<typename T, typename CallEvent>
void HomeNode::transmit_channel(std::deque<T>& txq, OutOpt<T>& port, CallEvent ev)
{
    if (!txq.empty() && port->can_put()) {
        auto pkt = txq.front();
        txq.pop_front();
        port->put(pkt);
        ev(pkt);
    }
}

bool HomeNode::arbitrate(Pipeline<HomeNodeTrackerEntry*>::Data req)
{
    auto entry = tracker.get_next_ready_entry();

    if (entry) {
        req = entry;
        ev_schedule(entry->request, entry->request->Opcode, entry->request->Addr, evd_txnid(entry->request->TxnID));
    }

    return true;
}

bool HomeNode::access(Pipeline<HomeNodeTrackerEntry*>::Data req)
{
    auto& entry = *req.get();

    auto sf_status = access_sf(entry);
    auto cache_status = access_cache(entry);

    ev_access(entry.request,
              entry.request->Opcode,
              entry.request->Addr,
              evd_txnid(entry.request->TxnID),
              entry.state.main,
              entry.sf_op,
              entry.cache_op,
              sf_status,
              cache_status);

    entry.sf_op = SFOp::None;
    entry.cache_op = CacheOp::None;

    tracker.process_event(entry, tracker_fsm::MainPipeStatus{sf_status, cache_status});

    if (entry.data_rsp_status == RespComp_UC
        && (cache_status == CacheStatus::HitClean || cache_status == CacheStatus::HitDirty))
    {
        entry.slc_hit_data_pending = true;
    }

    return true;
}

bool HomeNode::update(Pipeline<HomeNodeTrackerEntry*>::Data req)
{
    auto& entry = *req.get();

    if (entry.request_op)
        send_request(entry);

    if (entry.response_op)
        send_response(entry);

    if (entry.snoop_op)
        send_snoop(entry);

    if (entry.data_op)
        send_data(entry);

    if (tracker.can_dealloc(entry)) {
        if (!tracker.is_shadow_entry(entry)) {
            ev_dealloc(entry.request);
            auto requester = retry.dealloc(0);

            if (requester) {
                auto rsp = make<MeshRsp>(node_id, requester->src_id, requester->txn_id, ResponseOpcode::PCrdGrant);
                rsp->CBusyS = tracker.get_cbusy();
                rsp->PCrdType = 0;
                txrsp.push_back(rsp);
            }
        }

        if (entry.is_mem_request) {
            AMPERE_ASSERT(outstanding_requests > 0, "outstanding_requests dropped below 0");
            --outstanding_requests;
        }

        tracker.deallocate(entry);
    }

    return true;
}

SFStatus HomeNode::access_sf(HomeNodeTrackerEntry& entry)
{
    switch (entry.sf_op) {
    case SFOp::ReadLock:
        std::tie(entry.sf_set, entry.sf_way) = snoop_filter.lookup(entry.request, entry.request->Addr, false);

        if (entry.sf_way != -1) {
            snoop_filter.lock(entry.sf_set, entry.sf_way);
            entry.locked_sf = true;
            entry.snoop_status = snoop_filter.get_data(entry.sf_set, entry.sf_way);
            entry.snoop_address = snoop_filter.get_address(entry.sf_set, entry.sf_way);
            return SFStatus::Hit;
        }

        entry.sf_way = snoop_filter.find_empty_way(entry.request, entry.sf_set);

        if (entry.sf_way != -1) {
            snoop_filter.lock(entry.sf_set, entry.sf_way);
            entry.locked_sf = true;
            return SFStatus::Miss;
        }

        entry.sf_way = snoop_filter.find_victim(entry.request, entry.sf_set);

        if (entry.sf_way != -1) {
            entry.snoop_status = snoop_filter.get_data(entry.sf_set, entry.sf_way);
            entry.snoop_address = snoop_filter.get_address(entry.sf_set, entry.sf_way);
            snoop_filter.invalidate(entry.sf_set, entry.sf_way);
            snoop_filter.lock(entry.sf_set, entry.sf_way);
            entry.locked_sf = true;
            return SFStatus::Full;
        }

        return SFStatus::Lock;

    case SFOp::ReadSetWay:
        entry.snoop_status = snoop_filter.get_data(entry.sf_set, entry.sf_way);
        entry.snoop_address = snoop_filter.get_address(entry.sf_set, entry.sf_way);
        return SFStatus::Hit;

    case SFOp::SetUniqueUnlock:
        entry.snoop_status.reset();
        // LDID contains the actual Core NodeId
        entry.snoop_status.set_state(entry.request->LDID, FinalState::UC, false);
        snoop_filter.fill(entry.request, entry.sf_set, entry.sf_way, entry.request->Addr, entry.snoop_status, false);
        snoop_filter.update_lru(entry.sf_set, entry.sf_way);
        snoop_filter.unlock(entry.sf_set, entry.sf_way);
        entry.locked_sf = false;
        return SFStatus::Hit;

    case SFOp::SetSharedUnlock:
        // LDID contains the actual Core NodeId
        entry.snoop_status.set_state(entry.request->LDID, FinalState::SC, false);
        snoop_filter.fill(entry.request, entry.sf_set, entry.sf_way, entry.request->Addr, entry.snoop_status, false);
        snoop_filter.update_lru(entry.sf_set, entry.sf_way);
        snoop_filter.unlock(entry.sf_set, entry.sf_way);
        entry.locked_sf = false;
        return SFStatus::Hit;

    case SFOp::Invalidate:
        std::tie(entry.sf_set, entry.sf_way) = snoop_filter.lookup(entry.request, entry.request->Addr, false);
        if (entry.sf_way == -1 && lax_inclusion) {
            return SFStatus::Miss;
        } else {
            entry.snoop_status = snoop_filter.get_data(entry.sf_set, entry.sf_way);
            // LDID contains the actual Core NodeId
            entry.snoop_status.set_state(entry.request->LDID, FinalState::I, false);
            snoop_filter.fill(
                entry.request, entry.sf_set, entry.sf_way, entry.request->Addr, entry.snoop_status, false);
            snoop_filter.update_lru(entry.sf_set, entry.sf_way);
            return SFStatus::Hit;
        }

    case SFOp::Reset:
        entry.snoop_status.reset();
        snoop_filter.fill(entry.request, entry.sf_set, entry.sf_way, entry.request->Addr, entry.snoop_status, false);
        return SFStatus::Hit;

    case SFOp::ResumeAsMiss:
        return SFStatus::Miss;

    default:
        return SFStatus::Miss;
    }
}

CacheStatus HomeNode::access_cache(HomeNodeTrackerEntry& entry)
{
    if (!enable_slc)
        return CacheStatus::Miss;

    switch (entry.cache_op) {
    case CacheOp::Read:
        return slc.read(entry.request, entry.request->Addr);
    case CacheOp::ReadInvalidate:
        return slc.read_invalidate(entry.request, entry.request->Addr);
    case CacheOp::FillClean: {
        auto [status, addr] = slc.fill(entry.request, entry.request->Addr, NewSLC::State::CLEAN);
        if (status == CacheStatus::HitDirty)
            entry.cache_victim_addr = addr;
        return status;
    }
    case CacheOp::FillDirty: {
        auto [status, addr] = slc.fill(entry.request, entry.request->Addr, NewSLC::State::DIRTY);
        if (status == CacheStatus::HitDirty)
            entry.cache_victim_addr = addr;
        return status;
    }
    default:
        return CacheStatus::Miss;
    }
}

void HomeNode::send_request(HomeNodeTrackerEntry& entry)
{
    auto addr = entry.cache_victim_addr.has_value() ? *entry.cache_victim_addr : entry.request->Addr;
    auto req = make_clone<MeshReq>(entry.request, node_id, choose_mc(addr), entry.id, *entry.request_op, addr);

    if (entry.has_credit) {
        req->PCrdType = entry.retry_credit;
        req->AllowRetry = false;
    }

    // FIXME: not always true...probably need to build request in the FSM
    if ((entry.request_op == chi::RequestOpcode::WriteNoSnpFull)
        || (entry.request_op == chi::RequestOpcode::ReadNoSnp && entry.request->LikelyShared))
    {
        req->ReturnNID = node_id;
        req->ReturnTxnID = entry.id;
    } else {
        req->ReturnNID = entry.request->SrcID;
        req->ReturnTxnID = entry.request->TxnID;
    }

    // Want a ReadReceipt for this kind of read to memory
    // FIXME: seems kind of hokey to hard code this...
    if (entry.request_op == chi::RequestOpcode::ReadNoSnpSep)
        req->Order = RequestOrder::RequestAccepted;

    req->QoS = entry.request->QoS;

    txreq.push_back(req);
    entry.request_op.reset();
    entry.cache_victim_addr.reset();
}

void HomeNode::send_response(HomeNodeTrackerEntry& entry)
{
    auto response =
        make_clone<MeshRsp>(entry.request, node_id, entry.request->SrcID, entry.request->TxnID, *entry.response_op);
    response->DBID = entry.id;
    response->Resp = entry.data_rsp_status;
    response->CBusyS = tracker.get_cbusy();

    txrsp.push_back(response);

    entry.response_op.reset();
}

void HomeNode::send_snoop(HomeNodeTrackerEntry& entry)
{
    bool is_clean =
        entry.snoop_op == chi::SnoopOpcode::SnpMakeInvalid || entry.snoop_op == chi::SnoopOpcode::SnpCleanInvalid;

    auto cvs = entry.snoop_status.core_valid_as_set();
    auto pst = entry.snoop_status.primary_snoop_target();

    if (pst)
        cvs.insert(*pst);

    for (auto agent_id : cvs) {
        MeshSnp snoop_req;
        chi::SnoopOpcode opcode;

        if (agent_id == entry.snoop_status.primary_snoop_target())
            opcode = *entry.snoop_owner_op;
        else
            opcode = *entry.snoop_op;

        if (is_clean)
            snoop_req = make_child<MeshSnp>(entry.request, node_id, agent_id, entry.id, opcode, entry.snoop_address);
        else
            snoop_req = make_clone<MeshSnp>(entry.request, node_id, agent_id, entry.id, opcode, entry.snoop_address);

        // FIXME: Bah. This is not how to do this, but I need to make sure that cleans come back to us while other
        // kinds of snoops can be forwarded to the requesting core. I guess this needs another field in the tracker?
        //
        if (is_clean) {
            snoop_req->FwdNID = node_id;
            snoop_req->FwdTxnID = entry.id;
        } else {
            snoop_req->FwdNID = entry.request->SrcID;
            snoop_req->FwdTxnID = entry.request->TxnID;
        }

        txsnp.push_back(snoop_req);
    }

    entry.snoop_op.reset();
    entry.snoop_owner_op.reset();
}

void HomeNode::send_data(HomeNodeTrackerEntry& entry)
{
    if (entry.data_op == DataOpcode::CompData)
        send_data_to_requester(entry);
    else
        send_data_to_memory(entry);

    entry.data_op.reset();
}

void HomeNode::send_data_to_requester(HomeNodeTrackerEntry& entry)
{
    auto pkts = make_data(entry.request, node_id, entry.request->SrcID, entry.request->TxnID, *entry.data_op);

    for (auto& pkt : pkts) {
        pkt->HomeNID = node_id;
        pkt->DBID = entry.dbid;
        pkt->Resp = entry.data_rsp_status;
        pkt->CBusyS = tracker.get_cbusy();

        if (entry.slc_hit_data_pending)
            slc_hit_data_completion.put(pkt);
        else
            txdat.push_back(pkt);

        // This gets called generally in run for actually delayed responses
        // but in the case we have 0 latency in the response completion pipe
        // we can send the response immediately in the "normal" place
        send_delayed_data_to_requester();
    }

    entry.slc_hit_data_pending = false;
}

void HomeNode::send_delayed_data_to_requester()
{
    while (slc_hit_data_completion.can_take()) {
        auto dat = slc_hit_data_completion.take();
        txdat.push_back(dat);
    }
}

void HomeNode::send_data_to_memory(HomeNodeTrackerEntry& entry)
{
    auto pkts = make_data(entry.request, node_id, choose_mc(entry.request->Addr), entry.id, *entry.data_op);

    for (auto& pkt : pkts) {
        pkt->HomeNID = node_id;
        pkt->DBID = entry.dbid;
        pkt->CBusyS = tracker.get_cbusy();

        txdat.push_back(pkt);
    }
}

uint8_t HomeNode::data_id_to_chunk(uint8_t data_id)
{
    switch (config->data_bus_width()) {
    case 512:
        return 0xf;
    case 256:
        return data_id == 0 ? 0x3 : 0xc;
    case 128:
        return 1 << data_id;
    default:
        AMPERE_ERROR("unsupported data bus width {}", config->data_bus_width());
        return 0;
    }
}

NodeId HomeNode::choose_mc(PhysAddr addr)
{
    return sam->lookup(addr);
}

void HNCBusy::update(chi::CBusyState cbusy, int outstanding_reqs, bool retry)
{
    if (!enable)
        return;

    window_count++;

    switch (cbusy) {
    case chi::CBusyState::None: /* Not Busy, Throttle Up */
    case chi::CBusyState::Low:  /* Low Busy, Throttle Up */
        low_busy++;
        break;
    case chi::CBusyState::Medium: /* Medium Busy, Hold Level */
        medium_busy++;
        break;
    case chi::CBusyState::High:  /* High Busy, Throttle Down */
    case chi::CBusyState::Four:  /* High Busy, Throttle Down */
    case chi::CBusyState::Five:  /* High Busy, Throttle Down */
    case chi::CBusyState::Six:   /* High Busy, Throttle Down */
    case chi::CBusyState::Seven: /* High Busy, Throttle Down */
        high_busy++;
        break;
    };

    /* Window has closed, time to trigger a throttle event */
    if (window_count >= window) {
        bool throttling = false;

        if (high_busy > threshold_high)
            overall = chi::CBusyState::High;
        else if (medium_busy > threshold_medium)
            overall = chi::CBusyState::Medium;
        else if (low_busy > threshold_low)
            overall = chi::CBusyState::Low;

        if (overall == chi::CBusyState::High && c_outstanding.get_value() > throttle_min) {
            c_prefetcher = false;
            c_outstanding -= step_down;
            throttling = true;
        } else if (overall == chi::CBusyState::Low && c_outstanding.get_value() < throttle_max) {
            c_outstanding += step_up;
            c_prefetcher = true;
            throttling = true;
        }

        if (throttling)
            ev_throttle(evd_max_outstanding_requests(c_outstanding.get_value()), evd_prefetcher_enabled(c_prefetcher));

        reset_window(outstanding_reqs);
        medium_busy = 0; // not reset by reset window (yet)
    }
}

} // namespace ampere::mesh

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
