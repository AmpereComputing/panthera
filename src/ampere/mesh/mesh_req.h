//
// Copyright (c) 2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#ifndef AMPERE_MESH_REQ_H
#define AMPERE_MESH_REQ_H

#include "ampere/mesh/chi/types.h"
#include "ampere/sc/transaction.h"

namespace ampere::mesh {

class MeshReqObj : public chi::Request
{
public:
    MeshReqObj(chi::NodeId src_id, chi::NodeId tgt_id, uint16_t txn, chi::RequestOpcode opcode, archx::PhysAddr addr)
        : chi::Request{src_id, tgt_id, txn, opcode, addr}
    {
        active_requests[txid().value] = this;
    }
    bool measure_latency{false};

    ~MeshReqObj() override { active_requests.erase(txid().value); }

    static MeshReqObj* get_oldest_active_request();

private:
    static std::map<std::uint64_t, MeshReqObj*> active_requests;
};

using MeshReq = sc::transaction_ptr<MeshReqObj>;

} // namespace ampere::mesh

#endif // AMPERE_MESH_REQ_H

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
