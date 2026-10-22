//
// Copyright (c) 2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/mesh/test/dummy_memory.h"

namespace ampere::mesh {

bool DummyMemory::run()
{
    if (mesh_port.rx().req[0]->can_take() && mesh_port.tx().dat[0]->can_put()) {
        auto req = mesh_port.rx().req[0]->take();

        if (save_incoming_requests)
            saved_requests.push_back(req);

        if (req->Opcode == chi::RequestOpcode::ReadNoSnp) {
            auto dat = make_data_from_req(req, chi::DataOpcode::CompData);
            dat->Resp = chi::RespComp_UC;
            mesh_port.tx().dat[0]->put(dat);
        } else if (req->Opcode == chi::RequestOpcode::ReadNoSnpSep) {
            auto rsp = make<MeshRsp>(m_id, req->SrcID, req->TxnID, chi::ResponseOpcode::ReadReceipt);
            mesh_port.tx().rsp[0]->put(rsp);
            auto dat = make_data_from_req(req, chi::DataOpcode::DataSepResp);
            mesh_port.tx().dat[0]->put(dat);
        } else if (req->Opcode == chi::RequestOpcode::WriteNoSnpFull) {
            auto rsp = make<MeshRsp>(m_id, req->ReturnNID, req->ReturnTxnID, chi::ResponseOpcode::CompDBIDResp);
            rsp->DBID = get_dbid();
            mesh_port.tx().rsp[0]->put(rsp);
            pending_writes[rsp->DBID] = req->Addr;
        } else if (req->Opcode == chi::RequestOpcode::WriteNoSnpPtl) {
            auto rsp = make<MeshRsp>(m_id, req->ReturnNID, req->ReturnTxnID, chi::ResponseOpcode::CompDBIDResp);
            rsp->DBID = get_dbid();
            mesh_port.tx().rsp[0]->put(rsp);
            pending_writes[rsp->DBID] = req->Addr;
        } else if (req->Opcode == chi::RequestOpcode::PrefetchTgt) {
            prefetch_targets.insert(req->Addr);
        }

        else {
            AMPERE_FATAL("unexpected request opcode {}", req->Opcode);
        }
    }

    if (mesh_port.rx().dat[0]->can_take()) {
        auto dat = mesh_port.rx().dat[0]->take();
        AMPERE_ASSERT(dat->Opcode == chi::DataOpcode::NonCopyBackWrData, "unexpected data opcode {}", dat->Opcode);
        AMPERE_ASSERT(pending_writes.count(dat->DBID), "not waiting for write with DBID {}", dat->DBID);
        writes.insert(pending_writes[dat->DBID]);
        pending_writes.erase(dat->DBID);
    }

    return true;
}

MeshDat DummyMemory::make_data_from_req(MeshReq& req, chi::DataOpcode opcode)
{
    auto dat = make<MeshDat>(m_id, req->ReturnNID, req->ReturnTxnID, opcode);
    dat->HomeNID = req->SrcID;
    dat->DBID = req->TxnID;
    return dat;
}

uint16_t DummyMemory::get_dbid()
{
    AMPERE_ASSERT(!free_dbid_list.empty(), "ran out of DBIDs");
    auto id = free_dbid_list.front();
    free_dbid_list.pop_front();
    return id;
}

void DummyMemory::free_dbid(uint16_t id)
{
    free_dbid_list.push_back(id);
}

} // namespace ampere::mesh
