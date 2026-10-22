//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/archx/addr.h"
#include "ampere/archx/instruction_simple.h"

#include "ampere/core/testing/uops.h"

namespace ampere::core {

using namespace sc;

core::Uop create_nop_uop()
{
    std::shared_ptr<archx::Instruction> archx_instr = std::make_shared<archx::InstructionSimple>();
    auto archx_uops = furiosa::decode(archx_instr->opcode());
    auto archx_uop = std::move(archx_uops[0]);
    auto instr = trans_make<Instruction>(std::move(archx_instr), nullptr);
    auto uop = trans_make<Uop>(std::move(archx_uop), std::move(instr));
    uop->assign_latency(uop->raw_latency());
    uop->alloc_id = 0;
    return uop;
}

core::Uop create_branch_uop()
{
    auto fetch = trans_make<Fetch>(0x8888_pc, 0);
    fetch->update_prediction({BranchType::CONDITIONAL_DIRECT, 0x8888_pc, 0x9888_pc});
    auto archx_instruction = std::make_shared<archx::InstructionSimple>(0x8888_pc, 0x14000006);
    archx_instruction->set_next_pc(0x88a0_pc);
    auto archx_uops = furiosa::decode(archx_instruction->opcode());
    auto archx_uop = std::move(archx_uops[0]);
    auto instruction = trans_make<core::Instruction>(archx_instruction, fetch);
    archx_uop->end_inst = 1;
    auto uop = trans_make<core::Uop>(std::move(archx_uop), instruction);
    uop->assign_latency(uop->raw_latency());
    uop->alloc_id = 0;
    return uop;
}

core::Uop create_cond_branch_uop()
{
    auto fetch = trans_make<Fetch>(0x8888_pc, 0);
    fetch->update_prediction({BranchType::CONDITIONAL_DIRECT, 0x8888_pc, 0x9888_pc});
    auto archx_instruction = std::make_shared<archx::InstructionSimple>(0x8888_pc, 0x54000301);
    archx_instruction->set_next_pc(0x88e8_pc);
    auto archx_uops = furiosa::decode(archx_instruction->opcode());
    auto archx_uop = std::move(archx_uops[0]);
    auto instruction = trans_make<core::Instruction>(archx_instruction, fetch);
    archx_uop->end_inst = 1;
    auto uop = trans_make<core::Uop>(std::move(archx_uop), instruction);
    uop->assign_latency(uop->raw_latency());
    uop->alloc_id = 0;
    return uop;
}

core::Uop create_load_uop(VirtAddr virt_addr, PhysAddr phys_addr, ProgramCounter pc)
{
    constexpr std::uint32_t ldr_b_32_opcode = 0b00'111'0'01'01'000000000000'00000'00000;
    auto uop = create_core_uop(ldr_b_32_opcode, pc);
    uop->virt_addr = virt_addr;
    uop->phys_addr = phys_addr;
    return uop;
}

core::Uop create_store_uop(VirtAddr virt_addr, PhysAddr phys_addr, ProgramCounter pc)
{
    constexpr std::uint32_t str_b_32_opcode = 0b00'111'0'01'00'000000000000'00000'00000;
    auto uop = create_core_uop(str_b_32_opcode, pc);
    uop->virt_addr = virt_addr;
    uop->phys_addr = phys_addr;
    return uop;
}

core::Uop create_core_uop(std::uint32_t opcode, ProgramCounter pc)
{
    std::shared_ptr<archx::Instruction> archx_instr = std::make_shared<archx::InstructionSimple>(pc, opcode);
    auto archx_uops = furiosa::decode(archx_instr->opcode());
    AMPERE_ASSERT(archx_uops.size() == 1, "unexpected decode result");
    auto archx_uop = std::move(archx_uops[0]);
    auto instr = trans_make<Instruction>(std::move(archx_instr), nullptr);
    auto uop = trans_make<Uop>(std::move(archx_uop), std::move(instr));
    uop->assign_latency(uop->raw_latency());
    uop->alloc_id = 0;
    return uop;
}

} // namespace ampere::core
