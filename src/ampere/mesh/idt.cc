//
// Copyright (c) 2020-2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <algorithm>

#include "ampere/mesh/idt.h"

namespace ampere::mesh {

void IDT::set_a_to_b_lrid(NodeId lrid, uint8_t die_id)
{
    a_to_b_idt.lrid = lrid;
    a_to_b_idt.die_id = die_id;
}

void IDT::set_b_to_a_lrid(NodeId lrid, uint8_t die_id)
{
    b_to_a_idt.lrid = lrid;
    b_to_a_idt.die_id = die_id;
}

IDT::IDT(const sc_core::sc_module_name& name, sc::Clock* clock) : DieConnector{name, clock}
{
    //
    // This looks more complicated than it actually is. Basically, we're intercepting 3
    // channels (req, rsp0, dat) and bypassing 2 (rsp1 and snp) with an IDT Link. And
    // we do that in both directions.
    //

    // The "a to b" IDT link sits between the "b" side of the RDI and the "b" port. Its job
    // is to intercept requests headed from "a" to "b".
    //
    connect(rdi.b.tx().req[0], a_to_b_idt.rdi_port.rx().req[0]).name("a_to_b_from_rdi_req");
    connect(rdi.b.tx().rsp[0], a_to_b_idt.rdi_port.rx().rsp[0]).name("a_to_b_from_rdi_rsp0");
    connect(rdi.b.tx().dat[0], a_to_b_idt.rdi_port.rx().dat[0]).name("a_to_b_from_rdi_dat");

    a_to_b_idt.idt_port.tx().req[0].bind(b.tx().req[0]);
    a_to_b_idt.idt_port.tx().rsp[0].bind(b.tx().rsp[0]);
    a_to_b_idt.idt_port.tx().dat[0].bind(b.tx().dat[0]);

    rdi.b.tx().rsp[1].bind(b.tx().rsp[1]);
    rdi.b.tx().snp[0].bind(b.tx().snp[0]);

    a_to_b_idt.idt_port.rx().req[0].bind(b.rx().req[0]);
    a_to_b_idt.idt_port.rx().rsp[0].bind(b.rx().rsp[0]);
    a_to_b_idt.idt_port.rx().dat[0].bind(b.rx().dat[0]);

    connect(a_to_b_idt.rdi_port.tx().req[0], rdi.b.rx().req[0]).name("a_to_b_from_idt_req");
    connect(a_to_b_idt.rdi_port.tx().rsp[0], rdi.b.rx().rsp[0]).name("a_to_b_from_idt_rsp0");
    connect(a_to_b_idt.rdi_port.tx().dat[0], rdi.b.rx().dat[0]).name("a_to_b_from_idt_dat");

    rdi.b.rx().rsp[1].bind(b.rx().rsp[1]);
    rdi.b.rx().snp[0].bind(b.rx().snp[0]);

    connect(a_to_b_idt.out_credit_return, rdi.b_in_aux_credit).name("a_to_b_in_aux_credit");
    connect(rdi.b_out_aux_credit, a_to_b_idt.in_credit_return).name("a_to_b_out_aux_credit");

    // The "b to a" IDT link sits between the "a" side of the RDI and the "a" port. Its job
    // is to intercept requests headed from "b" to "a".
    //
    connect(rdi.a.tx().req[0], b_to_a_idt.rdi_port.rx().req[0]).name("b_to_a_from_rdi_req");
    connect(rdi.a.tx().rsp[0], b_to_a_idt.rdi_port.rx().rsp[0]).name("b_to_a_from_rdi_rsp0");
    connect(rdi.a.tx().dat[0], b_to_a_idt.rdi_port.rx().dat[0]).name("b_to_a_from_rdi_dat");

    b_to_a_idt.idt_port.tx().req[0].bind(a.tx().req[0]);
    b_to_a_idt.idt_port.tx().rsp[0].bind(a.tx().rsp[0]);
    b_to_a_idt.idt_port.tx().dat[0].bind(a.tx().dat[0]);

    rdi.a.tx().rsp[1].bind(a.tx().rsp[1]);
    rdi.a.tx().snp[0].bind(a.tx().snp[0]);

    b_to_a_idt.idt_port.rx().req[0].bind(a.rx().req[0]);
    b_to_a_idt.idt_port.rx().rsp[0].bind(a.rx().rsp[0]);
    b_to_a_idt.idt_port.rx().dat[0].bind(a.rx().dat[0]);

    connect(b_to_a_idt.rdi_port.tx().req[0], rdi.a.rx().req[0]).name("b_to_a_from_idt_req");
    connect(b_to_a_idt.rdi_port.tx().rsp[0], rdi.a.rx().rsp[0]).name("b_to_a_from_idt_rsp0");
    connect(b_to_a_idt.rdi_port.tx().dat[0], rdi.a.rx().dat[0]).name("b_to_a_from_idt_dat");

    rdi.a.rx().rsp[1].bind(a.rx().rsp[1]);
    rdi.a.rx().snp[0].bind(a.rx().snp[0]);

    connect(b_to_a_idt.out_credit_return, rdi.a_in_aux_credit).name("b_to_a_in_aux_credit");
    connect(rdi.a_out_aux_credit, b_to_a_idt.in_credit_return).name("b_to_a_out_aux_credit");
}

template<typename T>
bool IDTLink::transmit(TrafficBuffer<T>& q, OutOpt<T>& port, Event& send_event)
{
    if (!q.empty() && port->can_put()) {
        auto pkt = q.front();
        q.pop_front();
        port->put(pkt);
        send_event(pkt, pkt->Opcode, evd_srcid(pkt->SrcID.id()), evd_tgtid(pkt->TgtID.id()), evd_txnid(pkt->TxnID));
        return true;
    }
    return false;
}

IDTLink::IDTLink(const sc_core::sc_module_name& name, bool idm_mode) : MeshModule{name}, idm_mode{idm_mode}
{
    free_list.resize(rtrk_size);
    std::iota(free_list.begin(), free_list.end(), 0);

    for (size_t i = 0; i < rtrk_size; i++)
        rtrk_q.emplace_back(i);

    retry.define_credit_type(0, rtrk_size);

    rdi_port.rx().req[0].set_credit_pool(&rdi_in_req_credits);
    rdi_port.rx().rsp[0].set_credit_pool(&rdi_in_rsp_credits);
    rdi_port.rx().dat[0].set_credit_pool(&rdi_in_dat_credits);

    idt_port.rx().req[0].set_credit_pool(&idt_in_req_credits);
    idt_port.rx().rsp[0].set_credit_pool(&idt_in_rsp_credits);
    idt_port.rx().dat[0].set_credit_pool(&idt_in_dat_credits);
}

IDTLink::RTRKEntry* IDTLink::allocate_entry(MeshReq req)
{
    AMPERE_ASSERT(!full(), "cannot allocate into a full IDT Tracker");

    uint64_t n = free_list.front();
    free_list.pop_front();

    auto& entry = rtrk_q[n];
    AMPERE_ASSERT(entry.wait == 0, "All wait flags are not cleared");
    entry.req = req;
    ev_alloc(evd_occupancy(rtrk_size - free_list.size()));
    return &entry;
}

void IDTLink::deallocate_entry_if_not_waiting(RTRKEntry& entry)
{
    if (entry.wait) // deallocate only when all wait flags are cleared
        return;
    else {
        AMPERE_ASSERT(entry.id < rtrk_size && entry.id >= 0, "Entry ID is not valid");
        free_list.push_back(entry.id);
        entry.req = nullptr;
        out_credit_return->put({0, 1});
        ev_dealloc(evd_occupancy(rtrk_size - free_list.size()));
        AMPERE_ASSERT(outstanding_requests > 0, "Outstanding requests dropped below 0");
        outstanding_requests--;
    }
}

IDTLink::RTRKEntry* IDTLink::find_entry(uint64_t id)
{
    auto itr = std::find_if(rtrk_q.begin(), rtrk_q.end(), [id](const auto& el) -> bool { return el.id == id; });

    return (itr == rtrk_q.end()) ? nullptr : &(*itr);
}

void IDTLink::send_request(RTRKEntry& entry)
{
    auto req = entry.req;
    auto srcid = idm_mode ? req->SrcID : lrid;

    auto new_req = make_clone<MeshReq>(req, srcid, req->TgtID, entry.id, req->Opcode, req->Addr);
    new_req->LDID = req->SrcID;
    if (req->Opcode == chi::RequestOpcode::ReadNoSnpSep)
        new_req->Order = RequestOrder::RequestAccepted;

    if (idm_mode)
        new_req->ReturnTxnID = entry.id;

    if (entry.retry) {
        new_req->PCrdType = entry.pcrdtype;
        new_req->AllowRetry = false;
        entry.retry = false;
    }
    // std::cout << "IDT: " << name() << " Send Request: " << new_req->Opcode << " from " << new_req->SrcID << " to HN :
    // " << new_req->TgtID << std::endl;
    mesh_req_q.push_back(new_req);
    outstanding_requests++;
}

void IDTLink::send_response(RTRKEntry& entry, MeshRsp& rsp)
{
    auto req = entry.req;

    auto new_rsp = make_clone<MeshRsp>(
        rsp, entry.resp_src_id, req->SrcID, req->TxnID, rsp->Opcode); // here the same src id is used as the response
    *new_rsp = *rsp;
    new_rsp->SrcID = entry.resp_src_id;
    new_rsp->TgtID = req->SrcID;
    new_rsp->TxnID = req->TxnID;

    new_rsp->DBID = entry.id;
    new_rsp->CBusyS = get_cbusy();
    // std::cout << "IDT: " << name() << " Send Response: " << new_rsp->Opcode << " from " << new_rsp->SrcID << " to
    // Core : " << new_rsp->TgtID << std::endl;
    rdi_resp_q.push_back(new_rsp);
}

void IDTLink::send_response_to_completer(RTRKEntry& entry, MeshRsp& rsp)
{
    auto req = entry.req;
    auto srcid = idm_mode ? req->SrcID : lrid;

    MeshRsp new_rsp;
    // TODO change the way rsp fields are copied to new_rsp
    if (entry.wait & Wait4Data) {
        new_rsp = make_clone<MeshRsp>(rsp, srcid, req->TgtID, entry.dbid, rsp->Opcode); // early CompAck
        *new_rsp = *rsp;
        new_rsp->SrcID = srcid;
        new_rsp->TgtID = req->TgtID;
        new_rsp->TxnID = entry.dbid;
    } else {
        new_rsp =
            make_clone<MeshRsp>(rsp, lrid, entry.homenid, entry.dbid, rsp->Opcode); // send CompAck back to HomeAgent
        *new_rsp = *rsp;
        new_rsp->SrcID = lrid;
        new_rsp->TgtID = entry.homenid;
        new_rsp->TxnID = entry.dbid;
    }
    new_rsp->CBusyS = get_cbusy();
    // std::cout << "IDT: " << name() << " Send Response: " << new_rsp->Opcode << " from " << new_rsp->SrcID << " to
    // completer : " << new_rsp->TgtID << std::endl;
    mesh_resp_q.push_back(new_rsp);
}

void IDTLink::send_data(RTRKEntry& entry, DataOpcode op, ResponseStatus resp)
{
    auto req = entry.req;
    auto tgtid = idm_mode ? req->ReturnNID : req->SrcID;
    auto txnid = idm_mode ? req->ReturnTxnID : req->TxnID;
    auto pkts = make_data(req, entry.data_src_id, tgtid, txnid, op);

    for (auto& pkt : pkts) {
        pkt->DBID = idm_mode ? req->TxnID : entry.id;
        pkt->CBusyS = get_cbusy();
        pkt->Resp = resp;
        // std::cout << "IDT: " << name() << " Send Data: " << pkt->Opcode << " from " << pkt->SrcID << " to Core : " <<
        // pkt->TgtID << std::endl;
        rdi_data_q.push_back(pkt);
    }
}

void IDTLink::send_data_to_completer(RTRKEntry& entry, DataOpcode op, ResponseStatus resp)
{
    auto req = entry.req;
    auto srcid = idm_mode ? req->SrcID : lrid;
    auto pkts = make_data(req, srcid, req->TgtID, entry.dbid, op);

    for (auto& pkt : pkts) {
        pkt->CBusyS = get_cbusy();
        pkt->Resp = resp;
        // std::cout << "IDT: " << name() << " Send Data: " << pkt->Opcode << " from " << pkt->SrcID << " to completer :
        // " << pkt->TgtID << std::endl;
        mesh_data_q.push_back(pkt);
    }
}

void IDTLink::receive_requests_in_idtt(Event& recv_event)
{
    if (rdi_port.rx().req[0]->can_take()) {
        auto req = rdi_port.rx().req[0]->take();
        // std::cout << "IDTT: " << name() << " Received request : " << req->Opcode << " from " << req->SrcID <<
        // std::endl;
        if (req->Opcode != RequestOpcode::ReadNoSnpSep || idm_mode) {
            auto entry = allocate_entry(req);
            switch (req->Opcode) {
            case RequestOpcode::ReadNotSharedDirty:
            case RequestOpcode::ReadUnique:
            case RequestOpcode::ReadNoSnp:
                entry->wait |= (Wait4Comp | Wait4Data);
                entry->wait |= (entry->req->Order != RequestOrder::NoOrder) ? Wait4RRorRSD : 0;
                entry->wait |= entry->req->ExpCompAck ? Wait4CompAck : 0;
                break;
            case RequestOpcode::ReadNoSnpSep:
                entry->wait |= Wait4Data;
                entry->wait |= (entry->req->Order != RequestOrder::NoOrder) ? Wait4RRorRSD : 0;
                break;
            case RequestOpcode::Evict:
            case RequestOpcode::MakeUnique:
                entry->wait |= Wait4Comp;
                entry->wait |= entry->req->ExpCompAck ? Wait4CompAck : 0;
                break;
            case RequestOpcode::WriteBackFull:
            case RequestOpcode::WriteEvictFull:
            case RequestOpcode::WriteNoSnpFull:
                entry->wait |= (Wait4Comp | Wait4Data | Wait4DBID);
                entry->wait |= entry->req->ExpCompAck ? Wait4CompAck : 0;
                break;
            default:
                AMPERE_FATAL("Invalid Request opcode: {} received at IDT Target", req->Opcode);
            }
            send_request(*entry);
        } else {
            mesh_req_q.push_back(req);
        }
        recv_event(req, req->Opcode, evd_srcid(req->SrcID.id()), evd_tgtid(req->TgtID.id()), evd_txnid(req->TxnID));
    }
}

void IDTLink::receive_responses_from_completer(Event& recv_event)
{
    if (idt_port.rx().rsp[0].bind_count() && idt_port.rx().rsp[0]->can_take()) {
        auto rsp = idt_port.rx().rsp[0]->take();
        // std::cout << "IDT: " << name() << " Receive response: "<<  rsp->Opcode << " from " << rsp->SrcID <<
        // std::endl;
        auto txn_id =
            ((rsp->Opcode == chi::ResponseOpcode::PCrdGrant) ? retry_requester.pop_retry(rsp->PCrdType, rsp->SrcID)
                                                             : rsp->TxnID);
        RTRKEntry* entry = nullptr;
        if (rsp->TgtID.nodeid_type() == NodeId::NodeId_Type::Local || idm_mode)
            entry = find_entry(txn_id);
        if (entry) {
            // Entry found in the RTRK entry
            switch (rsp->Opcode) {
            case ResponseOpcode::RespSepData:
                entry->wait &= (~Wait4RRorRSD & ~Wait4Comp);
                entry->resp_src_id = rsp->SrcID;
                rcbusy.update(rsp->CBusyS, outstanding_requests);
                send_response(*entry, rsp);
                break;
            case ResponseOpcode::ReadReceipt:
                entry->wait &= (~Wait4RRorRSD);
                entry->resp_src_id = rsp->SrcID;
                rcbusy.update(rsp->CBusyS, outstanding_requests);
                send_response(*entry, rsp);
                break;
            case ResponseOpcode::CompDBIDResp:
                entry->wait &= (~Wait4DBID & ~Wait4Comp);
                entry->dbid = rsp->DBID;
                entry->resp_src_id = rsp->SrcID;
                rcbusy.update(rsp->CBusyS, outstanding_requests);
                send_response(*entry, rsp);
                break;
            case ResponseOpcode::DBIDResp:
                entry->wait &= (~Wait4DBID);
                entry->dbid = rsp->DBID;
                entry->resp_src_id = rsp->SrcID;
                rcbusy.update(rsp->CBusyS, outstanding_requests);
                send_response(*entry, rsp);
                break;
            case ResponseOpcode::Comp:
                entry->wait &= ~Wait4Comp;
                entry->dbid = rsp->DBID;
                entry->resp_src_id = rsp->SrcID;
                rcbusy.update(rsp->CBusyS, outstanding_requests);
                send_response(*entry, rsp);
                break;
            case ResponseOpcode::RetryAck:
                entry->pcrdtype = rsp->PCrdType;
                entry->retry = true;
                rcbusy.update(rsp->CBusyS, outstanding_requests, true);
                retry_requester.push_retry(rsp->PCrdType, rsp->SrcID, txn_id);
                AMPERE_ASSERT(outstanding_requests > 0, "Outstanding requests dropped below 0");
                outstanding_requests--;
                break;
            case ResponseOpcode::PCrdGrant:
                rcbusy.update(rsp->CBusyS, outstanding_requests);
                send_request(*entry);
                break;
            default:
                AMPERE_FATAL("Response Opcode: {} not supported", rsp->Opcode);
            }
        } else
            rdi_resp_q.push_back(rsp);

        recv_event(rsp, rsp->Opcode, evd_srcid(rsp->SrcID.id()), evd_tgtid(rsp->TgtID.id()), evd_txnid(rsp->TxnID));
    }
}

void IDTLink::receive_responses_from_requester(Event& recv_event)
{
    if (rdi_port.rx().rsp[0].bind_count() && rdi_port.rx().rsp[0]->can_take()) {
        auto rsp = rdi_port.rx().rsp[0]->take();
        // std::cout << "IDT: " << name() << " Receive response: "<<  rsp->Opcode << " from " << rsp->SrcID << " to " <<
        // rsp->TgtID << std::endl;
        auto txid = rsp->TxnID;
        RTRKEntry* entry = nullptr;
        AMPERE_ASSERT(rsp->TgtID.nodeid_type() == NodeId::NodeId_Type::Global, "TgtID type is incorrect");
        if ((rsp->Opcode == ResponseOpcode::CompAck) && (rsp->TgtID.die_id() == die_id)) {
            entry = find_entry(txid);
            if (entry) {
                entry->wait &= ~Wait4CompAck;
                send_response_to_completer(*entry, rsp);
                deallocate_entry_if_not_waiting(*entry);
            } else
                AMPERE_FATAL("Response entry not found");
        } else
            mesh_resp_q.push_back(rsp);
        recv_event(rsp, rsp->Opcode, evd_srcid(rsp->SrcID.id()), evd_tgtid(rsp->TgtID.id()), evd_txnid(rsp->TxnID));
    }
}

void IDTLink::receive_data_from_completer(Event& recv_event)
{
    if (idt_port.rx().dat[0].bind_count() && idt_port.rx().dat[0]->can_take()) {
        auto dat = idt_port.rx().dat[0]->take();
        recv_event(dat, dat->Opcode, evd_srcid(dat->SrcID.id()), evd_tgtid(dat->TgtID.id()), evd_txnid(dat->TxnID));
        // std::cout << "IDT: " << name() << " Receive data: "<<  dat->Opcode << " from " << dat->SrcID << " to " <<
        // dat->TgtID <<std::endl;
        RTRKEntry* entry = nullptr;
        if (dat->TgtID.nodeid_type() == NodeId::NodeId_Type::Local || idm_mode)
            entry = find_entry(dat->TxnID);
        if (entry) {
            if (dat->DataID == 0) {
                // Entry found in the RTRK
                switch (dat->Opcode) {
                case DataOpcode::CompData:
                case DataOpcode::DataSepResp:
                    entry->wait &= (~Wait4Comp & ~Wait4Data);
                    entry->dbid = dat->DBID;
                    entry->homenid = dat->HomeNID;
                    entry->data_src_id = dat->SrcID;
                    rcbusy.update(dat->CBusyS, outstanding_requests);
                    send_data(*entry, dat->Opcode, dat->Resp);
                    deallocate_entry_if_not_waiting(*entry);
                    break;
                default:
                    AMPERE_FATAL("Data opcode: {} not supported", dat->Opcode);
                }
            }
        } else
            rdi_data_q.push_back(dat);
    }
}

void IDTLink::receive_data_from_requester(Event& recv_event)
{
    if (rdi_port.rx().dat[0].bind_count() && rdi_port.rx().dat[0]->can_take()) {
        auto dat = rdi_port.rx().dat[0]->take();
        recv_event(dat, dat->Opcode, evd_srcid(dat->SrcID.id()), evd_tgtid(dat->TgtID.id()), evd_txnid(dat->TxnID));
        RTRKEntry* entry = nullptr;
        AMPERE_ASSERT(dat->TgtID.nodeid_type() == NodeId::NodeId_Type::Global, "TgtID type is incorrect");
        if ((dat->Opcode == DataOpcode::CopyBackWrData) && (dat->TgtID.die_id() == die_id)) {
            entry = find_entry(dat->TxnID);
            if (entry) {
                if (dat->DataID == 0) {
                    // Entry found in the RTRK
                    entry->wait &= ~Wait4Data;
                    send_data_to_completer(*entry, dat->Opcode, dat->Resp);
                    deallocate_entry_if_not_waiting(*entry);
                }
            } else
                AMPERE_FATAL("Data entry not found");
        } else
            mesh_data_q.push_back(dat);
    }
}

void IDTLink::transmit_from_idtt()
{
    // send the request to the IDT port
    if (outstanding_requests < rcbusy.num_requests()) {
        transmit(mesh_req_q, idt_port.tx().req[0], ev_send_req);
    }
    // send the response to the IDT port
    transmit(mesh_resp_q, idt_port.tx().rsp[0], ev_send_rsp);
    // send the data to the IDT port
    transmit(mesh_data_q, idt_port.tx().dat[0], ev_send_dat);
    // send response back to RDI port
    transmit(rdi_resp_q, rdi_port.tx().rsp[0], ev_send_rsp);
    // send data back to RDI port
    transmit(rdi_data_q, rdi_port.tx().dat[0], ev_send_dat);
}

bool IDTLink::run()
{
    transmit_from_idtt();
    transmit_from_idts();
    receive_requests_in_idtt(ev_recv_req);
    receive_requests_in_idts(ev_recv_req);
    receive_responses_from_completer(ev_recv_rsp);
    receive_responses_from_requester(ev_recv_rsp);
    receive_data_from_completer(ev_recv_dat);
    receive_data_from_requester(ev_recv_dat);
    receive_rtrk_credits();

    return (!mesh_req_q.empty() || !rdi_req_q.empty() || !rdi_resp_q.empty() || !rdi_data_q.empty()
            || !mesh_resp_q.empty() || !mesh_data_q.empty());
}

void IDTLink::receive_requests_in_idts(Event& recv_event)
{
    if (idt_port.rx().req[0].bind_count() && idt_port.rx().req[0]->can_take()) {
        auto req = idt_port.rx().req[0]->take();
        // std::cout << "IDTS: " << name() << " Received request : " << req->Opcode << " from " << req->SrcID << " to "
        // << req->TgtID << std::endl;
        auto [can_process, pcrd_type] = retry.process_request(req);

        if (can_process) {
            rdi_req_q.push_back(req);
        } else {
            // std::cout << "Retry ack sent " << std::endl;
            auto rsp = make_clone<MeshRsp>(
                req, lrid, req->SrcID, req->TxnID, ResponseOpcode::RetryAck); // TODO handle idm_enable case
            rsp->PCrdType = pcrd_type;
            rsp->CBusyS = get_cbusy();
            mesh_resp_q.push_back(rsp);
            ev_retry_sent();
        }
        recv_event(req, req->Opcode, evd_srcid(req->SrcID.id()), evd_tgtid(req->TgtID.id()), evd_txnid(req->TxnID));
    }
}

void IDTLink::receive_rtrk_credits()
{
    while (in_credit_return->can_take()) {
        auto [type, count] = in_credit_return->take();

        for (; count; --count) {
            auto requester = retry.dealloc(0);

            if (requester) {
                // std::cout << "IDT: " << name() << " PCrdGrant sent " << std::endl;
                auto rsp = make<MeshRsp>(lrid,
                                         requester->src_id,
                                         requester->txn_id,
                                         ResponseOpcode::PCrdGrant); // TODO handle idm_enable case
                rsp->PCrdType = 0;
                rsp->CBusyS = get_cbusy();
                mesh_resp_q.push_back(rsp);
                ev_pcrd_sent();
            }
        }
    }
}

void IDTLink::transmit_from_idts()
{
    transmit(rdi_req_q, rdi_port.tx().req[0], ev_send_req);
}

} // namespace ampere::mesh
