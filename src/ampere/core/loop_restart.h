//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#ifndef AMPERE_CORE_LOOP_RESTART_H
#define AMPERE_CORE_LOOP_RESTART_H

#include "ampere/archx/addr.h"
#include "ampere/sc/sc.h"
#include "ampere/sc/transaction.h"
#include "ampere/core/fetch.h"

namespace ampere {
namespace core {

using namespace ampere::sc;
using namespace ampere::archx;

class LoopRestartObj;
using LoopRestart = ampere::sc::transaction_ptr<LoopRestartObj>;

class LoopRestartObj : public ampere::sc::Transaction
{
public:
    LoopRestartObj(BPUBank bank,
                   ProgramCounter pc,
                   ProgramCounter prev_pc,
                   ProgramCounter branch_pc,
                   ProgramCounter target_pc,
                   size_t completed_iterations)
        : bank(bank),
          pc(pc),
          prev_pc(prev_pc),
          branch_pc(branch_pc),
          target_pc(target_pc),
          completed_iterations(completed_iterations)
    {
    }

    BPUBank bank;
    ProgramCounter pc;
    ProgramCounter prev_pc;
    ProgramCounter branch_pc;
    ProgramCounter target_pc;
    size_t completed_iterations;
};

} // namespace core
} // namespace ampere

#endif // AMPERE_CORE_LOOP_RESTART_H

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
