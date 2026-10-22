//
// Copyright (c) 2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#ifndef AMPERE_MESH_RESP_H
#define AMPERE_MESH_RESP_H

#include "ampere/mesh/chi/types.h"
#include "ampere/sc/transaction.h"

namespace ampere::mesh {

class MeshRspObj : public chi::Response
{
public:
    MeshRspObj(chi::NodeId src_id, chi::NodeId tgt_id, uint16_t txn, chi::ResponseOpcode opcode)
        : chi::Response(src_id, tgt_id, txn, opcode)
    {
    }
};

using MeshRsp = sc::transaction_ptr<MeshRspObj>;

} // namespace ampere::mesh

#endif // AMPERE_MESH_RESP_H

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
