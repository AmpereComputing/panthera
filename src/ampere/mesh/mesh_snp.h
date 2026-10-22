//
// Copyright (c) 2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#ifndef AMPERE_MESH_SNOOP_REQ_H
#define AMPERE_MESH_SNOOP_REQ_H

#include "ampere/mesh/chi/types.h"
#include "ampere/sc/transaction.h"

namespace ampere::mesh {

class MeshSnpObj : public chi::Snoop
{
public:
    MeshSnpObj(chi::NodeId src_id, chi::NodeId tgt_id, uint16_t txn, chi::SnoopOpcode opcode, archx::PhysAddr addr)
        : chi::Snoop{src_id, txn, opcode, addr}, TgtID{tgt_id}
    {
    }
    // CHI spec states that snoops are broadcast/multicast.  For now we have simplifed routing by using a
    // single target.
    chi::NodeId TgtID;
};

using MeshSnp = sc::transaction_ptr<MeshSnpObj>;

} // namespace ampere::mesh

#endif // AMPERE_MESH_SNOOP_REQ_H

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
