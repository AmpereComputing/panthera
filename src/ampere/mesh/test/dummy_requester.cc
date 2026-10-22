//
// Copyright (c) 2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/mesh/test/dummy_requester.h"
#include "ampere/mesh/mesh_req.h"

namespace ampere::mesh {

bool DummyRequester::run()
{
    transmit();
    receive();
    return true;
}

void DummyRequester::transmit()
{
    transmit_channel<MeshReq>(txreq, mesh_port.tx().req[0]);
    transmit_channel<MeshRsp>(txrsp, mesh_port.tx().rsp[0]);
    transmit_channel<MeshSnp>(txsnp, mesh_port.tx().snp[0]);
    transmit_channel<MeshDat>(txdat, mesh_port.tx().dat[0]);
}

template<typename T>
void DummyRequester::transmit_channel(std::deque<T>& txq, OutOpt<T>& port)
{
    if (!txq.empty() && port->can_put()) {
        auto pkt = txq.front();
        txq.pop_front();
        port->put(pkt);
    }
}

void DummyRequester::receive()
{
    if (mesh_port.rx().rsp[0]->can_take()) {
        auto rsp = mesh_port.rx().rsp[0]->take();
        if (announcer.size())
            announcer->response(rsp->Opcode, rsp->TxnID, rsp->DBID);
    }

    if (mesh_port.rx().rsp[1]->can_take()) {
        auto rsp = mesh_port.rx().rsp[1]->take();
        if (announcer.size())
            announcer->response(rsp->Opcode, rsp->TxnID, rsp->DBID);
    }

    if (mesh_port.rx().snp[0]->can_take()) {
        auto snp = mesh_port.rx().snp[0]->take();
        if (announcer.size())
            announcer->snoop(snp->Opcode, snp->TxnID);
    }

    if (mesh_port.rx().dat[0]->can_take()) {
        auto dat = mesh_port.rx().dat[0]->take();
        if (announcer.size())
            announcer->data(dat->Opcode, dat->TxnID, dat->DBID);
    }
}

void DummyRequester::send_request(NodeId tgt, uint16_t txn, RequestOpcode opcode, PhysAddr address, RequestOrder order)
{
    auto req = make<MeshReq>(m_id, tgt, txn, opcode, address);
    req->Order = order;
    txreq.push_back(req);
}

void DummyRequester::send_request_retry(NodeId tgt, uint16_t txn, RequestOpcode opcode, PhysAddr address)
{
    auto req = make<MeshReq>(m_id, tgt, txn, opcode, address);
    req->AllowRetry = false;
    txreq.push_back(req);
}

void DummyRequester::send_request_ls(NodeId tgt, uint16_t txn, RequestOpcode opcode, PhysAddr address)
{
    auto req = make<MeshReq>(m_id, tgt, txn, opcode, address);
    req->LikelyShared = true;
    txreq.push_back(req);
}

void DummyRequester::send_request(MeshReq req)
{
    req->SrcID = m_id;
    txreq.push_back(req);
}

void DummyRequester::send_snoop(NodeId tgt, uint16_t txn, SnoopOpcode opcode, PhysAddr address)
{
    auto snp = make<MeshSnp>(m_id, tgt, txn, opcode, address);
    txsnp.push_back(snp);
}

void DummyRequester::send_credited_request(
    NodeId tgt, uint16_t txn, RequestOpcode opcode, PhysAddr address, uint8_t credit)
{
    auto req = make<MeshReq>(m_id, tgt, txn, opcode, address);
    req->PCrdType = credit;
    req->AllowRetry = false;
    txreq.push_back(req);
}

void DummyRequester::send_response(NodeId tgt, uint16_t txn, ResponseOpcode opcode)
{
    auto rsp = make<MeshRsp>(m_id, tgt, txn, opcode);
    txrsp.push_back(rsp);
}

void DummyRequester::send_response(NodeId tgt, uint16_t txn, ResponseOpcode opcode, uint16_t dbid)
{
    auto rsp = make<MeshRsp>(m_id, tgt, txn, opcode);
    rsp->DBID = dbid;
    if (opcode == ResponseOpcode::SnpResp)
        rsp->Resp = RespSnoop_I;
    txrsp.push_back(rsp);
}

void DummyRequester::send_response(NodeId tgt, uint16_t txn, ResponseOpcode opcode, uint16_t dbid, ResponseStatus resp)
{
    auto rsp = make<MeshRsp>(m_id, tgt, txn, opcode);
    rsp->DBID = dbid;
    rsp->Resp = resp;
    txrsp.push_back(rsp);
}

void DummyRequester::send_response(MeshRsp rsp)
{
    rsp->SrcID = m_id;
    txrsp.push_back(rsp);
}

void DummyRequester::send_data(NodeId tgt, uint16_t txn, DataOpcode opcode, uint16_t dbid, ResponseStatus resp)
{
    AMPERE_ASSERT(opcode == chi::DataOpcode::CopyBackWrData || opcode == chi::DataOpcode::SnpRespData
                      || opcode == chi::DataOpcode::CompData,
                  "not expecting data opcode {}",
                  opcode);

    if (opcode == chi::DataOpcode::CopyBackWrData)
        txn = dbid;

    auto dat = make<MeshDat>(m_id, tgt, txn, opcode);
    dat->Resp = resp;
    txdat.push_back(dat);
}

void DummyRequester::send_data(MeshDat dat)
{
    dat->SrcID = m_id;
    txdat.push_back(dat);
}

} // namespace ampere::mesh
