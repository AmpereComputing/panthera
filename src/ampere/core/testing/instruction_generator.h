//
// Copyright (c) 2018-2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include <queue>

#include "ampere/sc/sc.h"
#include "ampere/core/fetch.h"
#include "ampere/core/instruction.h"

#include "ampere/archx/addr.h"
#include "ampere/archx/instruction_simple.h"

namespace ampere::core {

using namespace ampere::archx;

class InstructionGenerator : public sc::Taker<Instruction>
{
public:
    bool can_take() const override;
    Instruction take() override;
    void clear() override;
    void clear_if(const std::function<bool(const Instruction&)>& pred) override { clear(); }
    const Instruction& peek(uint64_t index = 0) const override { throw std::runtime_error{"no peeking"}; }
    uint64_t num_ready() const override;

    void add_instruction(uint32_t opcode);
    void add_instruction(uint32_t opcode, VirtAddr va);
    void add_instruction(uint32_t opcode, ProgramCounter pc);
    void add_instruction(uint32_t opcode, ProgramCounter pc, VirtAddr va);
    void set_max_instructions(size_t count) { max_instructions = count; }

private:
    ProgramCounter default_pc{0xdeadbeef00000000};
    uint32_t default_opcode{archx::nop_opcode};

    struct InsnInfo
    {
        ProgramCounter pc;
        uint32_t opcode;
        VirtAddr va;
    };

    InsnInfo get_next_insn();
    size_t max_instructions{1};
    size_t instruction_count{0};
    FetchId current_fetch_id{1};
    std::queue<InsnInfo> instruction_queue;
    sc::CreditPool instruction_fifo{"instruction_fifo"};
    sc::CreditPool feq{"feq"};
};

} // namespace ampere::core

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
