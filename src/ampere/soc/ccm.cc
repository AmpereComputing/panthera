//
// Copyright (c) 2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/soc/ccm.h"
#include "ampere/mesh/mesh_req.h"
#include "ampere/uarch_common/memop.h"

namespace ampere::soc {

Ccm::Ccm(const sc_core::sc_module_name& name) : MeshModule(name)
{
    // L2 connections
    connect(core.l2c.mesh_request, request).name("l2_mesh_request");
    connect(response, core.l2c.mesh_response).name("l2_mesh_response");

    // Snoop Filter connections
    connect(snoop, core.l2c.mesh_snoop).name("l2_mesh_snoop");

    free_txns.resize(txn_outstanding_max);
    std::iota(free_txns.begin(), free_txns.end(), 0);
}

bool Ccm::run()
{
    // Generate mesh requests from core mem requests
    core_handle_requests();

    // Handle incoming data from mesh
    mesh_handle_data();

    // Handle incoming snoops from mesh
    mesh_handle_snoops();

    // Send any queued up trafic to the mesh
    mesh_send_traffic();

    // Handle memory requests from cores
    core_send_responses();
    core_receive_requests();

    // Capture any type of traffic coming from mesh
    mesh_receive_traffic();

    return true;
}

void Ccm::core_handle_requests()
{
    while (!core_reqs.empty()) {
        if (mesh_port.tx.req->can_put() && !free_txns.empty()) {
            auto req = core_reqs.front();

            chi::RequestOpcode opcode;

            switch (req->type) {
            case MemOp::READ:
                opcode = chi::RequestOpcode::ReadNotSharedDirty;
                break;
            case MemOp::READUNIQUE:
                opcode = chi::RequestOpcode::ReadUnique;
                break;
            case MemOp::WRITE:
                AMPERE_FATAL("MemOp::WRITE Request not implemented");
                break;
            case MemOp::SNOOP:
                AMPERE_FATAL("MemOp::SNOOP Request not implemented");
                break;
            case MemOp::EVICT:
                AMPERE_FATAL("MemOp::EVICT Request not implemented");
                break;
            default:
                AMPERE_FATAL("Unknown MemOp Request not implemented");
            };

            int txn_id = free_txns.front();
            free_txns.pop_front();

            MeshReq mesh_req =
                make_child<MeshReq>(req, node_id, config->nodeid_from_addr(req->phys_addr), txn_id, opcode);
            mesh_req->Addr = req->phys_addr;
            mesh_reqs.push(mesh_req);
            core_reqs.pop();
            outstanding_reqs[txn_id] = {req->id, mesh_req};
            // printf("Queue MeshReq TxnID: %d, addr 0x%lx\n", mesh_req->TxnID, req->phys_addr.value());
        } else
            return;
    };
}

void Ccm::core_receive_requests()
{
    while (request->can_take()) {
        auto req = request->take();
        core_reqs.push(req);
    }
}

void Ccm::core_send_responses()
{
    while (response->can_put() && !core_rsps.empty()) {
        response->put(core_rsps.front());
        core_rsps.pop();
    }
}

void Ccm::mesh_receive_traffic()
{
    while (mesh_port.rx.req->can_take()) {
        auto req = mesh_port.rx.req->take();
        AMPERE_FATAL("Unhandled traffic type request from mesh");
    }

    while (mesh_port.rx.rsp0->can_take()) {
        auto rsp = mesh_port.rx.rsp0->take();
        AMPERE_FATAL("Unhandled traffic type response from mesh");
    }

    while (mesh_port.rx.snp->can_take()) {
        auto snp = mesh_port.rx.snp->take();
        mesh_snps.push(snp);
        ev_recv_snp(snp, snp->Opcode, snp->Addr);
    }

    while (mesh_port.rx.dat->can_take()) {
        auto dat = mesh_port.rx.dat->take();
        mesh_incoming_data.push(dat);
        MeshReq req;
        std::tie(std::ignore, req) = outstanding_reqs.at((int)dat->TxnID);
        ev_recv_dat(req, dat->Opcode);
    }
}

void Ccm::mesh_send_traffic()
{
    // Send queued up requests to mesh
    while (!mesh_reqs.empty() && mesh_port.tx.req->can_put()) {
        mesh_port.tx.req->put(mesh_reqs.front());
        auto mesh_req = mesh_reqs.front();
        mesh_reqs.pop();
        ev_send_req(mesh_req, mesh_req->Opcode, mesh_req->Addr, evd_txnid(mesh_req->TxnID));
        // printf("Send MeshReq TxnID: %d\n", mesh_req->TxnID);
    }

    // Send queued up responses to mesh
    while (!mesh_rsps.empty() && mesh_port.tx.rsp0->can_put()) {
        auto mesh_rsp = mesh_rsps.front();
        mesh_port.tx.rsp0->put(mesh_rsp);
        mesh_rsps.pop();
        ev_send_rsp(mesh_rsp, mesh_rsp->Opcode);
        // printf("Send MeshRsp TxnID: %d\n", mesh_rsp->TxnID);
    }
}

void Ccm::mesh_handle_snoops()
{
    while (!mesh_snps.empty()) {
        auto snp = mesh_snps.front();
        mesh_snps.pop();

        switch (snp->Opcode) {
        case chi::SnoopOpcode::SnpMakeInvalid:
            // TODO Do something with this cache.invalidate(snp, snp->Addr);
            make_response(snp, chi::ResponseOpcode::SnpResp);
            break;
        case chi::SnoopOpcode::SnpCleanInvalid:
            make_response(snp, chi::ResponseOpcode::SnpResp);
            // TODO look at mesh_injector for source of hits and misses
            break;
        default:
            AMPERE_ERROR("unexpected snoop opcode {}", snp->Opcode);
            break;
        };
    }
}

/** Create a snoop response and put it on the response queue.
 *
 * @param snp The snoop request which prompted this response.
 * @param opcode The response opcode.
 */
void Ccm::make_response(MeshSnp snp, chi::ResponseOpcode opcode)
{
    auto rsp = make_clone<MeshRsp>(snp, node_id, snp->FwdNID, snp->FwdTxnID, opcode);
    rsp->Resp = chi::RespSnoop_I; // FIXME: obviously not always the case
    mesh_rsps.push(rsp);
}

void Ccm::mesh_handle_data()
{
    while (!mesh_incoming_data.empty()) {
        if (response->can_put()) {
            auto dat = mesh_incoming_data.front();
            auto [req_id, req] = outstanding_reqs.at((int)dat->TxnID);

            MemOp::Type type;

            if (req->Opcode == chi::RequestOpcode::ReadNotSharedDirty)
                type = MemOp::Type::READ;
            else if (req->Opcode == chi::RequestOpcode::ReadUnique)
                type = MemOp::Type::READUNIQUE;
            else
                AMPERE_ERROR("unexpected request opcode {}", req->Opcode);

            MemResp mem_resp = make_child<MemResp>(
                req, req_id, req->Addr, false, type); // TODO Currently all requests are not in cache.
            core_rsps.push(mem_resp);

            // TODO: Should this not be freed until the core gets the mem transaction response?
            outstanding_reqs.erase(dat->TxnID);
            free_txns.push_back(dat->TxnID);

            // Send Response back to home node
            MeshRsp mesh_rsp = make_clone<MeshRsp>(req, node_id, req->TgtID, dat->DBID, chi::ResponseOpcode::CompAck);
            mesh_rsps.push(mesh_rsp);
            // printf("Queue MeshRsp Response TxnID = %d, Addr 0x%lx\n", dat->TxnID, req->second.value());

            mesh_incoming_data.pop();
        }
    };
}

} // namespace ampere::soc

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
