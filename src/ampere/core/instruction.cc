//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/core/instruction.h"
#include "ampere/archx/branch_target.h"

namespace ampere::core {

std::optional<archx::FlushType> InstructionObj::requires_flush() const
{
    // catches loads that fault to EL1 for example
    bool non_sequential = archx_instruction->next_program_counter() != archx_instruction->program_counter() + 4;
    bool taken_branch = archx_instruction->is_taken_branch();

    if (aborted) {
        return archx::FlushType::ABORTED;
    } else if (archx_instruction->is_eret()) {
        return archx::FlushType::ERET;
    } else if (archx_instruction->is_exception()) {
        switch (archx_instruction->exception_type()) {
        case archx::ExceptionType::HLT:
            return archx::FlushType::DBG_HALT;
        case archx::ExceptionType::BRK:
            return archx::FlushType::INSTR_DEBUG;
        case archx::ExceptionType::SVC:
            return archx::FlushType::SVC;
        case archx::ExceptionType::HVC:
            return archx::FlushType::HVC;
        case archx::ExceptionType::SMC:
            return archx::FlushType::SMC;
        case archx::ExceptionType::ISB:
            return archx::FlushType::CSF_WTH_CMT;
        default:
            AMPERE_FATAL("Detected unexpected exception type in InstructionObj::requires_flush()");
        }
    } else if (non_sequential
               && (!taken_branch || archx_instruction->branch_target() != archx_instruction->next_program_counter()))
    {
        // This case handles faults and other asynchronous exceptions that we
        // cannot otherwise detect using our instruction stream, so detect PC
        // discontinuities not from branches and treat them as such. Note that
        // these can occur even if the current instruction is a branch.
        return archx::FlushType::NOT_KNOWN;
    } else if (archx_instruction->is_tlbi() && looping) {
        return archx::FlushType::LOOPING_TLBI;
    }

    return std::nullopt;
}

} // namespace ampere::core
