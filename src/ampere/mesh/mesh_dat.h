//
// Copyright (c) 2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#ifndef AMPERE_MESH_DATA_H
#define AMPERE_MESH_DATA_H

#include "ampere/mesh/chi/types.h"
#include "ampere/sc/transaction.h"

namespace ampere::mesh {

class MeshDatObj;

using MeshDat = sc::transaction_ptr<MeshDatObj>;

class MeshDatObj : public chi::Data
{
public:
    MeshDatObj(chi::NodeId src_id, chi::NodeId tgt_id, uint16_t txn, chi::DataOpcode opcode)
        : chi::Data{src_id, tgt_id, txn, opcode}
    {
    }

    bool measure_latency{false};
    // Testing only
    MeshDatObj(uint16_t txn, chi::DataOpcode opcode) : chi::Data{chi::NodeId{0}, chi::NodeId{0}, txn, opcode} {}
};

} // namespace ampere::mesh

#endif // AMPERE_MESH_DATA_H

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
