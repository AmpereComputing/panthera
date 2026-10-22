
//
// Copyright (c) 2019-2024 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include <boost/make_unique.hpp>
#include "ampere/archx/branch_target.h"
#include "ampere/archx/instruction_is.h"
#include "ampere/archx/instruction_simple.h"
#include "ampere/archx/furiosa.h"
#include "ampere/archx/fusion_delay_ripley.h"
#include "ampere/archx/driver_is.h"
#include "ampere/archx/emulate.h"
#include "ampere/sc/report.h"
#include "ampere/archx/instruction_is.h"
#include "ampere/archx/inum.h"

namespace ampere::archx {

ampere::is::Features InstructionIS::stream_features;

const uint32_t InstructionIS::DEFAULT_OPCODE = 0x91002c21;

void InstructionIS::set_opcode(std::uint32_t opcode)
{
    stream_instruction.set_opcode(opcode);
}

InstructionIS::InstructionIS() : InstructionIS(ProgramCounter{0}) {}

InstructionIS::InstructionIS(ProgramCounter pc, DecoderMode decoder_mode) : pc{pc}
{
    dec_mode = decoder_mode;
    set_opcode(DEFAULT_OPCODE); // add x1, x1, #11
    next_pc = pc + 4;
}

InstructionIS::InstructionIS(ProgramCounter pc,
                             std::uint32_t opcode,
                             std::optional<ArchState> state,
                             DecoderMode decoder_mode)
    : pc(pc)
{
    dec_mode = decoder_mode;
    if (state.has_value()) {
        arch_state = state.value();
        arch_state_output = arch_state;
    }
    set_opcode(opcode);

    if (is_branch() && is_direct(branch_type()) && is_unconditional(branch_type())) {
        next_pc = branch_target_if_taken(pc, this->opcode());
    } else {
        next_pc = pc + 4;
    }
}

InstructionIS::InstructionIS(ProgramCounter pc,
                             std::uint32_t opcode,
                             inum_t instruction_id,
                             inum_t prior_instruction_id,
                             ArchState state,
                             std::optional<SimulationControl> control,
                             DecoderMode decoder_mode)
    : pc{pc},
      instruction_id(instruction_id),
      prior_instruction_id(prior_instruction_id),
      arch_state(state),
      simulation_control(control)
{
    arch_state_output = arch_state;
    dec_mode = decoder_mode;
    set_opcode(opcode);
    next_pc = pc + 4;
}

InstructionIS::InstructionIS(const ampere::is::Instruction& stream_instruction,
                             inum_t instruction_id,
                             inum_t prior_instruction_id,
                             ArchState state,
                             DecoderMode decoder_mode)
    : stream_instruction(stream_instruction),
      instruction_id(instruction_id),
      prior_instruction_id(prior_instruction_id),
      arch_state(state)
{
    arch_state_output = arch_state;
    dec_mode = decoder_mode;
    pc = ProgramCounter(stream_instruction.program_counter().va());
}
InstructionIS::InstructionIS(const ampere::is::Instruction& stream_instruction,
                             inum_t instruction_id,
                             ArchState state,
                             DecoderMode decoder_mode)
    : stream_instruction(stream_instruction), instruction_id(instruction_id), prior_instruction_id(0), arch_state(state)
{
    arch_state_output = arch_state;
    dec_mode = decoder_mode;
    pc = ProgramCounter(stream_instruction.program_counter().va());
}

InstructionIS::InstructionIS(const ampere::is::Instruction& stream_instruction,
                             inum_t instruction_id,
                             inum_t prior_instruction_id,
                             ArchState state,
                             std::optional<SimulationControl> control,
                             DecoderMode decoder_mode)
    : stream_instruction(stream_instruction),
      instruction_id(instruction_id),
      prior_instruction_id(prior_instruction_id),
      arch_state(state),
      simulation_control(control)
{
    arch_state_output = arch_state;
    dec_mode = decoder_mode;
    pc = ProgramCounter(stream_instruction.program_counter().va());
}

PhysAddr ampere::archx::InstructionIS::program_counter_pa() const
{
    return PhysAddr(stream_instruction.program_counter().pa());
}

VirtAddr ampere::archx::InstructionIS::zva_address() const
{
    auto rt = cvt_gpr(opcode() & 0b11111);

    return VirtAddr(state().read(rt));
}

uint32_t ampere::archx::InstructionIS::num_addresses() const
{
    if (is_emulated) {
        return emulated_virt_addr.size();
    } else if (!stream_features.dc_zva_addresses() && (is_dc_zva() || is_dc_gzva()))
        return 1;
    else
        return stream_instruction.memop_size();
}

bool ampere::archx::InstructionIS::memop_predicated_off(unsigned n) const
{
    if (wrong_path()) { // FIXME: emulation changes this
        return false;
    }
    if (has_exception()) {
        return false;
    }

    assert(n < num_addresses());
    return stream_instruction.memop(n).predicated_off();
}

std::uint32_t ampere::archx::InstructionIS::memop_bytes(unsigned n) const
{
    // return number of memory bytes referenced
    if (wrong_path()) { // FIXME: emulation should handle this
        return 1;
    }
    if (has_exception()) {
        return 1;
    }
    assert(n < num_addresses());
    return stream_instruction.memop(n).bytes();
}

VirtAddr ampere::archx::InstructionIS::virtual_address(unsigned n) const
{
    // Need to handle Ld/St instructions that do not have VirtAddrs.
    if (!stream_features.dc_zva_addresses() && (is_dc_zva() || is_dc_gzva())) {
        return zva_address();
    } else if (is_emulated && n < emulated_virt_addr.size()) {
        return use_emulated_address(n);
    } else if (n < num_addresses()) {
        return VirtAddr(stream_instruction.memop(n).va());
    }
    return VirtAddr{0};
}

PhysAddr ampere::archx::InstructionIS::virt_to_phys(unsigned n) const
{
    // Translate this instruction's virtual_address() to physical, returning
    // 0x0 upon fault

    // GPR prefetch instructions targeting instructions should be treated as I-side
    const bool is_instr = is_gpr_prefetch() && prefetch_op(opcode()) == PrefetchOp::PLI;
    auto tr = Translation(virtual_address(n),
                          Bitfield(state().read(TCR_EL1)),
                          Bitfield(state().read(TTBR0_EL1)),
                          Bitfield(state().read(TTBR1_EL1)),
                          is_instr);
    while (!tr.done()) {
        auto next_pte = tr.next_pte();
        uint64_t next_pte_contents = state().read(next_pte);
        tr.pte_fetched(next_pte_contents);
    }
    if (!tr.faulted())
        return tr.result_address();
    return PhysAddr(0);
}

PhysAddr ampere::archx::InstructionIS::physical_address(unsigned n) const
{
    // Need to handle Ld/St instructions that do not have PhysAddrs.
    // For now, return zero address.
    if (!stream_features.dc_zva_addresses() && (is_dc_zva() || is_dc_gzva())) {
        return virt_to_phys(n);
    } else if (is_emulated && n < emulated_virt_addr.size()) {
        return virt_to_phys(n);
    } else if (n < num_addresses()) {
        return PhysAddr(stream_instruction.memop(n).pa());
    }
    return PhysAddr(0);
}

void ampere::archx::InstructionIS::execute()
{
    throw std::runtime_error("unimplemented");
}

UopSet InstructionIS::decode(DecoderMode decoder_mode)
{
    UopSet uops = Instruction::decode(decoder_mode);

    if (uops.empty()) {
        // Unable to to decode
        if (!wrong_path())
            AMPERE_WARNING("Unable to decode opcode {:08x}, replacing with nop", opcode());
        InstructionSimple nop(program_counter(), nop_opcode);
        uops = nop.decode();
        uops.back()->end_inst = 1;
    }
    return uops;
}

// #define DEBUG_AGEN
VirtAddr InstructionIS::use_emulated_address(unsigned n) const
{
    if (is_emulated && emulated_virt_addr.size() > n) {
        VirtAddr addr = emulated_virt_addr[n];
#if defined(DEBUG_AGEN)
        fmt::print(stderr, "AGEN {} -refs-> {:016x} (emulated)\n", disasm(), addr.value());
#endif
        return addr;
    }
    return VirtAddr(0);
}

} // namespace ampere::archx

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
