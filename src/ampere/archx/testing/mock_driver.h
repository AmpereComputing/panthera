//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include "ampere/archx/addr.h"
#include "ampere/archx/driver.h"
#include "ampere/archx/instruction_simple.h"
#include <gmock/gmock.h>

namespace ampere::archx {

class MockDriver : public Driver
{
public:
    std::shared_ptr<archx::InstructionSimple> return_nop(ProgramCounter pc = 0_pc) { return return_op(pc); }
    std::shared_ptr<archx::InstructionSimple> return_b(ProgramCounter pc = 0_pc)
    {
        return return_op(pc, 0x14000008); // unconditionally branch to PC + 0x20
    }
    std::shared_ptr<archx::InstructionSimple> return_op(ProgramCounter pc = 0_pc, std::uint32_t op = archx::nop_opcode)
    {
        return std::make_shared<archx::InstructionSimple>(pc, op, pc);
    }
    std::shared_ptr<archx::InstructionSimple> return_memop(ProgramCounter pc = 0_pc,
                                                           std::uint32_t op = archx::nop_opcode,
                                                           VirtAddr addr = 0x0_va)
    {
        return std::make_shared<archx::InstructionSimple>(pc, op, addr);
    }
    std::shared_ptr<archx::InstructionSimple> return_load(ProgramCounter pc = 0_pc, VirtAddr addr = 0x0_va)
    {
        constexpr std::uint32_t ldr_b_32_opcode = 0b00'111'0'01'01'000000000000'00000'00000;
        return return_memop(pc, ldr_b_32_opcode, addr);
    }
    std::shared_ptr<archx::InstructionSimple> return_store(ProgramCounter pc = 0_pc, VirtAddr addr = 0x0_va)
    {
        constexpr std::uint32_t str_b_32_opcode = 0b00'111'0'01'00'000000000000'00000'00000;
        return return_memop(pc, str_b_32_opcode, addr);
    }

    MOCK_METHOD(ProgramCounter, next_pc, (bool*), (override));
    MOCK_METHOD(void, commit, (std::shared_ptr<archx::Instruction>), (override));
    MOCK_METHOD(std::shared_ptr<archx::Instruction>, fetch, (ProgramCounter), (override));
    MOCK_METHOD(void, recover, (inum_t), (override));
    MOCK_METHOD(uint64_t, read_memory, (PhysAddr), (const, override));
    MOCK_METHOD(uint64_t, read_sysreg, (SysReg), (const, override));
    MOCK_METHOD(EL, exception_level, (), (const, override));
    MOCK_METHOD(uint16_t, asid, (), (const, override));
    MOCK_METHOD(uint64_t, committed_id, (), (const, override));
};

} // namespace ampere::archx

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
