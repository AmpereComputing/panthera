//
// Copyright (c) 2024-2025 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#pragma once

#include "ampere/sc/sc.h"

make_ns_enum(ampere::memory, HIFOp, RD, WR);

namespace ampere::memory {

using archx::PhysAddr;
using sc::Transaction;
using sc::transaction_ptr;

/** HIFCmdObj represents a command to the memory controller core
 *  through the HIF interface. It is trivially simple; just two
 *  opcodes: RD and WR.
 *
 *  Along with the opcode is the (full physical) address and an
 *  id which is used to return data back to the CHP.
 */
struct HIFCmdObj : public Transaction
{
    HIFOp op;
    PhysAddr addr;
    bool high_priority;
    int id;

    HIFCmdObj(HIFOp op, PhysAddr addr, bool high_priority, int id)
        : op{op}, addr{addr}, high_priority{high_priority}, id{id}
    {
    }
};

using HIFCmd = transaction_ptr<HIFCmdObj>;
using HIFDat = int; // the only data returning to the CHP is the id of the RD

} // namespace ampere::memory
