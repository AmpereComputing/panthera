//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#ifndef AMPERE_CORE_FLUSH_H
#define AMPERE_CORE_FLUSH_H

#include "ampere/archx/addr.h"
#include "ampere/archx/flush_type.h"
#include "ampere/core/fetch.h"
#include "ampere/core/uop.h"
#include "ampere/sc/sc.h"
#include "ampere/sc/transaction.h"

namespace ampere {
namespace core {

using namespace ampere::sc;
using namespace ampere::archx;

class FlushObj;
using Flush = ampere::sc::transaction_ptr<FlushObj>;

class FlushObj : public ampere::sc::Transaction
{
public:
    FlushObj(ProgramCounter pc,
             ProgramCounter flushing_pc,
             Fetch fetch,
             Instruction instruction,
             uint64_t alloc_id,
             FlushType type)
        : pc(pc), flushing_pc(flushing_pc), instruction(instruction), fetch(fetch), alloc_id(alloc_id), type(type)
    {
    }

    explicit FlushObj(const Instruction& instruction, FlushType type, uint64_t alloc_id = 0)
        : pc(instruction->archx_instruction->next_program_counter()),
          flushing_pc(instruction->archx_instruction->program_counter()),
          instruction(instruction),
          fetch(instruction->fetch),
          alloc_id(alloc_id),
          type(type),
          mispredicted_direction(instruction->is_mispredicted_direction()),
          mispredicted_target(instruction->is_mispredicted_target())
    {
    }

    explicit FlushObj(const Uop& uop, FlushType type) : FlushObj(uop->flush_instruction(), type, uop->alloc_id)
    {
        // RAW scrub flushes will restart from 'scrubbed' uop and not
        // the next uop/instruction.  Most flush mechanism assumes
        // next uop, thus the "minus 1" applied to alloc_id.
        if (type == FlushType::RAW_SCRUB) {
            pc = flushing_pc;
            alloc_id = uop->alloc_id - 1;
        }
    }

    ProgramCounter pc;
    ProgramCounter flushing_pc;
    Instruction instruction;
    Fetch fetch;
    uint64_t alloc_id;
    FlushType type;

    bool mispredicted_direction{false};
    bool mispredicted_target{false};
    bool is_context_change{false};

    uint64_t memory_buffer_id{0};

    bool is_branch_check() const
    {
        return (type == FlushType::BRANCH_CHECK_UNCOND_BRANCH || type == FlushType::BRANCH_CHECK_COND_BRANCH
                || type == FlushType::BRANCH_CHECK_CALL_DIRECT || type == FlushType::BRANCH_CHECK_CALL_INDIRECT
                || type == FlushType::BRANCH_CHECK_UNCOND_INDIRECT || type == FlushType::BRANCH_CHECK_RETURN
                || type == FlushType::BRANCH_CHECK_TARGET || type == FlushType::BRANCH_CHECK_WRONG_TYPE);
    }
};

} // namespace core
} // namespace ampere

#endif // AMPERE_CORE_FLUSH_H

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
