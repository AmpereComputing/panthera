//
// Copyright (c) 2018-2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#ifndef PANTHERA_CORE_INSTRUCTION_H
#define PANTHERA_CORE_INSTRUCTION_H

#include <optional>

#include "ampere/archx/instruction.h"
#include "ampere/archx/flush_type.h"
#include "ampere/core/fetch.h"
#include "ampere/core/prediction.h"
#include "ampere/sc/transaction.h"
#include "ampere/sc/credit.h"
#include "ampere/core/gather.h"
#include "ampere/archx/inum.h"

namespace ampere::core {

using archx::ProgramCounter;
using sc::Credit;

class InstructionObj : public ampere::sc::Transaction
{
public:
    InstructionObj(std::shared_ptr<ampere::archx::Instruction> archx_instruction, Fetch fetch)
        : archx_instruction(archx_instruction), fetch{fetch}
    {
    }

    bool is_taken_branch() const { return archx_instruction->is_taken_branch(); }

    bool predicted_taken_branch() const { return is_fetch_exit() && fetch->get_prediction().is_taken(); }

    bool is_mispredicted_direction() const
    {
        const bool mispredicted_dir = is_taken_branch() != predicted_taken_branch();
        if (is_unconditional(archx_instruction->branch_type())) {
            // never mispredict dir for uncond branches
            return false;
        }

        if (wrong_path()) {
            if (archx_instruction->emulated()) {
                return mispredicted_dir;
            }
            // we don't know enough to know if it mispredicted so assume predicted correctly
            return false;
        }

        return mispredicted_dir;
    }

    ProgramCounter predicted_next_pc() const
    {
        if (is_fetch_exit())
            return fetch->next_program_counter();
        else
            return archx_instruction->program_counter() + 4;
    }

    bool is_mispredicted_target() const
    {
        bool taken_to_wrong_target = is_taken_branch() && (predicted_next_pc() != next_pc());
        if (wrong_path()) {
            if (archx_instruction->emulated()) {
                return taken_to_wrong_target;
            }
            // Allow (non-emulated) wrong-path indirect branches to
            // always have correctly-predicted targets because we do
            // not have enough information available to determine
            // their real targets.
            return false;
        }

        return taken_to_wrong_target;
    }

    // Determine if we're mispredicting due to a branch type mismatch. We only
    // care about the type if this is the taken branch that ends the fetch.
    bool is_mispredicted_branch_type() const
    {
        if (is_fetch_exit()) {
            auto predicted_branch_type = fetch->get_prediction().branch_type;
            return (predicted_branch_type != BranchType::NONE && predicted_branch_type != BranchType::UNSPECIFIED
                    && predicted_branch_type != archx_instruction->branch_type());
        }
        return false;
    }

    bool is_fetch_exit() const { return archx_instruction->program_counter() == fetch->exit_pc(); }
    adis::encoding_enum_t adis_info() const { return archx_instruction->adis_info(); }
    bool can_fuse_movprfx_predicated() const { return archx_instruction->inst_info().is_movprfx_fusible_predicated; }
    bool can_fuse_movprfx_unpredicated() const
    {
        return archx_instruction->inst_info().is_movprfx_fusible_unpredicated;
    }
    bool is_branch() const { return archx_instruction->is_branch(); }
    ProgramCounter pc() const { return archx_instruction->program_counter(); }
    std::uint32_t opcode() const { return archx_instruction->opcode(); }
    ProgramCounter next_pc() const { return archx_instruction->next_program_counter(); }
    archx::inum_t inum() const { return archx_instruction->inum(); }
    archx::inum_t prev_inum() const { return archx_instruction->prev_inum(); }
    bool wrong_path() const { return archx_instruction->wrong_path(); }
    bool emulated() const { return archx_instruction->emulated(); }
    archx::EL exception_level() const { return archx_instruction->state().exception_level(); }
    std::optional<archx::FlushType> requires_flush() const;

    std::shared_ptr<ampere::archx::Instruction> archx_instruction;
    Credit fifoid;
    bool fused_with_unconditional_branch{false};
    bool has_internal_cleaving{false};

    bool aborted{false};

    bool loop_start{false};
    bool loop_end{false};
    bool loop_abort{false};
    bool looping{false};

    Fetch fetch;
    archx::PhysAddr phys_addr;
    GatherProxyCreditPtr gather_proxy{nullptr};
};

using Instruction = ampere::sc::transaction_ptr<InstructionObj>;
using ConstInstruction = ampere::sc::const_ptr<Instruction>;

} // namespace ampere::core

#endif // PANTHERA_CORE_INSTRUCTION_H

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
