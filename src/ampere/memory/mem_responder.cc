//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include "ampere/memory/mem_responder.h"

namespace ampere::memory {

using namespace mesh::chi;

/* This is a simple model designed to connect to a core and provide fixed
 * latencies for memory responses. It is intended for Core PV work.
 */

bool MemResponder::run()
{
    receive();
    send();

    // schedule a future wakeup on our next operation completion
    schedule_wakeup();

    return false;
}

void MemResponder::receive()
{
    if (mesh_port.rx().rsp[0]->can_take()) {
        MeshRsp rsp = mesh_port.rx().rsp[0]->take();
        ev_recv(rsp, rsp->Opcode, evd_srcid(rsp->SrcID.id()), evd_tgtid(rsp->TgtID.id()), evd_txnid(rsp->TxnID));

        switch (rsp->Opcode) {
        case ResponseOpcode::CompAck:
            break;
        default:
            AMPERE_ERROR("unexpected response opcode {}", rsp->Opcode);
        }
    }

    if (mesh_port.rx().dat[0]->can_take()) {
        MeshDat dat = mesh_port.rx().dat[0]->take();
        ev_recv(dat, dat->Opcode, evd_srcid(dat->SrcID.id()), evd_tgtid(dat->TgtID.id()), evd_txnid(dat->TxnID));

        auto chunks_valid = awaiting_data.find(dat->TxnID);

        if (chunks_valid == awaiting_data.end())
            dump(std::cout);

        AMPERE_ASSERT(chunks_valid != awaiting_data.end(), "unexpected data received for DBID {}", dat->DBID);

        chunks_valid->second |= data_id_to_chunks(dat->DataID);

        if (all_chunks_received(chunks_valid->second)) {
            awaiting_data.erase(dat->TxnID);
        }
    }

    if (mesh_port.rx().req[0]->can_take()) {
        MeshReq req = mesh_port.rx().req[0]->take();
        ev_recv(
            req, req->Addr, req->Opcode, evd_srcid(req->SrcID.id()), evd_tgtid(req->TgtID.id()), evd_txnid(req->TxnID));

        switch (req->Opcode) {
        case RequestOpcode::ReadUnique:
            do_read(req, FinalState::UC);
            break;
        case RequestOpcode::ReadNotSharedDirty:
            do_read(req, FinalState::UC); // FIXME: dunno what the real state is here
            break;
        case RequestOpcode::Evict:
            do_evict(req);
            // rem_transaction(req->TxnID);
            break;
        case RequestOpcode::MakeUnique:
            do_makeunique(req);
            break;
        case RequestOpcode::WriteEvictFull:
        case RequestOpcode::WriteBackFull:
        case RequestOpcode::WriteNoSnpFull:
        case RequestOpcode::WriteNoSnpPtl:
            do_write_back(req);
            break;
        default:
            AMPERE_ERROR("unexpected request opcode {}", req->Opcode);
        }
    }
}

void MemResponder::send()
{
    auto now = get_cycle();

    while (mesh_port.tx().rsp[0]->can_put() && !rsp_queue.empty() && rsp_queue.begin()->first <= now) {
        auto rsp = rsp_queue.begin()->second;
        mesh_port.tx().rsp[0]->put(rsp);
        ev_send(rsp, rsp->Opcode, evd_srcid(rsp->SrcID.id()), evd_tgtid(rsp->TgtID.id()), evd_txnid(rsp->TxnID));
        rsp_queue.erase(rsp_queue.begin());
    }

    while (mesh_port.tx().dat[0]->can_put() && !dat_queue.empty() && dat_queue.begin()->first <= now) {
        auto dat = dat_queue.begin()->second;
        mesh_port.tx().dat[0]->put(dat);
        ev_send(dat, dat->Opcode, evd_srcid(dat->SrcID.id()), evd_tgtid(dat->TgtID.id()), evd_txnid(dat->TxnID));
        dat_queue.erase(dat_queue.begin());
    }
}

void MemResponder::do_read(MeshReq req, FinalState state)
{
    DataOpcode data_opcode;

    if (separate_response_and_data) {
        auto rsp = make_clone<MeshRsp>(req, node_id, req->SrcID, req->TxnID, ResponseOpcode::RespSepData);
        rsp->Resp = ResponseStatus{ResponseStatus::Type::Comp, state};
        rsp_queue.emplace(get_cycle() + rsp_delay, rsp);
        data_opcode = DataOpcode::DataSepResp;
    } else {
        data_opcode = DataOpcode::CompData;
    }

    auto pkts = make_data(req, node_id, req->SrcID, req->TxnID, data_opcode);

    for (auto& dat : pkts) {
        dat->HomeNID = node_id;
        dat->DBID = req->TxnID;
        dat->Resp = ResponseStatus{ResponseStatus::Type::Comp, state};

        dat_queue.emplace(get_cycle() + dat_delay, dat);
    }
}

void MemResponder::do_evict(MeshReq req)
{
    auto rsp = make_clone<MeshRsp>(req, node_id, req->SrcID, req->TxnID, ResponseOpcode::Comp);
    rsp->Resp = RespComp_I;
    rsp_queue.emplace(get_cycle() + rsp_delay, rsp);
}

void MemResponder::do_makeunique(MeshReq req)
{
    auto rsp = make_clone<MeshRsp>(req, node_id, req->SrcID, req->TxnID, ResponseOpcode::Comp);
    rsp->Resp = RespComp_UC;
    rsp->DBID = req->TxnID; // is this true?
    rsp_queue.emplace(get_cycle() + rsp_delay, rsp);
}

void MemResponder::do_write_back(MeshReq req)
{
    auto rsp = make_clone<MeshRsp>(req, node_id, req->SrcID, req->TxnID, ResponseOpcode::CompDBIDResp);
    rsp->DBID = req->TxnID;
    rsp_queue.emplace(get_cycle() + rsp_delay, rsp);

    awaiting_data[req->TxnID] = 0;
}

void MemResponder::do_comp_ack(MeshRsp rsp)
{
    auto req = awaiting_comp_ack.find(rsp->TxnID);
    AMPERE_ASSERT(req != awaiting_comp_ack.end(), "got unexpected CompAck for TxnID={}", rsp->TxnID);
    awaiting_comp_ack.erase(req);
}

uint8_t MemResponder::data_id_to_chunks(uint8_t data_id) const
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

bool MemResponder::all_chunks_received(uint8_t chunks) const
{
    return chunks == 0xf;
}

void MemResponder::schedule_wakeup()
{
    auto now = get_cycle();
    auto next_rsp = rsp_queue.begin();
    auto next_dat = dat_queue.begin();

    Cycle wake_cycle{0};

    if (next_rsp != rsp_queue.end()) {
        if (next_dat != dat_queue.end())
            wake_cycle = std::min(next_rsp->first, next_dat->first);
        else
            wake_cycle = next_rsp->first;
    } else if (next_dat != dat_queue.end()) {
        wake_cycle = next_dat->first;
    }

    if (wake_cycle != 0) {
        CycleDelta cycle_offset = wake_cycle > now ? wake_cycle - now : 1;
        get_clock()->schedule(this, cycle_offset);
    }
}

void MemResponder::add_transaction(MeshReq req)
{
    if (req->TxnID >= transactions.size())
        transactions.resize(req->TxnID + 1);

    if (transactions[req->TxnID])
        dump();

    AMPERE_ASSERT(!transactions[req->TxnID], "already tracking TxnID={}", req->TxnID);

    transactions[req->TxnID] = req;
}

void MemResponder::rem_transaction(uint16_t txnid)
{
    if (!transactions[txnid])
        dump();
    AMPERE_ASSERT(transactions[txnid], "not tracking transaction TxnID={}", txnid);
    transactions[txnid] = nullptr;
}

void MemResponder::dump(std::ostream& out) const
{
    fmt::print(out, "{}", name());
    fmt::print(out, "=== Transactions ================================\n");
    for (size_t i = 0; i < transactions.size(); ++i) {
        auto& t = transactions[i];

        if (!t) {
            fmt::print(out, "{:2d}: EMPTY\n", i);
        } else {
            fmt::print(out, "{:2d}: Opcode={} Addr={}\n", i, t->Opcode, t->Addr);
        }
    }
    fmt::print(out, "================================================\n");
}

} // namespace ampere::memory
