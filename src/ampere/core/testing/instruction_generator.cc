//
// Copyright (c) 2018-2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/core/testing/instruction_generator.h"

#include <cassert>
#include <boost/smart_ptr/make_unique.hpp>

namespace ampere::core {

using namespace core;

bool InstructionGenerator::can_take() const
{
    return (instruction_count < max_instructions) && instruction_fifo.can_acquire() && feq.can_acquire();
}

uint64_t InstructionGenerator::num_ready() const
{
    return can_take() ? 1 : 0;
}

Instruction InstructionGenerator::take()
{
    InsnInfo insn = get_next_insn();

    auto fetch = sc::trans_make<Fetch>(insn.pc, current_fetch_id++);
    auto archx_instruction = boost::make_unique<archx::InstructionSimple>(insn.pc, insn.opcode, insn.va);
    auto instruction = boost::make_local_shared<InstructionObj>(std::move(archx_instruction), fetch);
    if (instruction->is_fetch_exit())
        fetch->feq_credit = feq.acquire();
    instruction->fifoid = instruction_fifo.acquire();
    instruction_count++;
    return instruction;
}

InstructionGenerator::InsnInfo InstructionGenerator::get_next_insn()
{
    InsnInfo info;

    if (instruction_queue.empty()) {
        info = {default_pc, default_opcode, VirtAddr{0}};
        default_pc += 4;
    } else {
        info = instruction_queue.front();
        instruction_queue.pop();
    }

    return info;
}

void InstructionGenerator::clear()
{
    while (!instruction_queue.empty())
        instruction_queue.pop();
}

void InstructionGenerator::add_instruction(uint32_t opcode)
{
    instruction_queue.push(InsnInfo{default_pc, opcode, VirtAddr{0}});
    default_pc += 4;
}

void InstructionGenerator::add_instruction(uint32_t opcode, VirtAddr va)
{
    add_instruction(opcode, default_pc, va);
    default_pc += 4;
}

void InstructionGenerator::add_instruction(uint32_t opcode, ProgramCounter pc)
{
    add_instruction(opcode, pc, VirtAddr{0});
}

void InstructionGenerator::add_instruction(uint32_t opcode, ProgramCounter pc, VirtAddr va)
{
    instruction_queue.push(InsnInfo{pc, opcode, va});
}

} // namespace ampere::core

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
