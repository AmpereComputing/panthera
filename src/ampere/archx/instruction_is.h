//
// Copyright (c) 2019-2024 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#pragma once
#include <optional>
#include <set>
#include <vector>
#include "ampere/archx/uop.h"
#include "ampere/archx/instruction.h"
#include "ampere/archx/decoder_mode.h"
#include "ampere/archx/arch_state.h"
#include "ampere/instruction_stream/instruction_stream.pb.h"
#include "ampere/adis/src/adis.h"
#include "ampere/archx/inum.h"

namespace ampere::archx {

class InstructionIS : public Instruction
{
public:
    static const uint32_t DEFAULT_OPCODE;

    InstructionIS();
    // The default instruction is: add x1, x1, #0xb
    InstructionIS(ProgramCounter pc, DecoderMode decoder_mode = DecoderModeDefault);
    InstructionIS(ProgramCounter pc,
                  std::uint32_t opcode,
                  std::optional<ArchState> state = std::nullopt,
                  DecoderMode decoder_mode = DecoderModeDefault);
    InstructionIS(ProgramCounter pc,
                  std::uint32_t opcode,
                  inum_t instruction_id,
                  inum_t prior_instruction_id,
                  ArchState state,
                  std::optional<SimulationControl> control = std::nullopt,
                  DecoderMode decoder_mode = DecoderModeDefault);

    // stream-based constructors
    InstructionIS(const ampere::is::Instruction& stream_instruction,
                  inum_t instruction_id,
                  inum_t prior_instruction_id,
                  ArchState state,
                  DecoderMode decoder_mode = DecoderModeDefault);

    InstructionIS(const ampere::is::Instruction& stream_instruction,
                  inum_t instruction_id,
                  ArchState state,
                  DecoderMode decoder_mode = DecoderModeDefault);

    InstructionIS(const ampere::is::Instruction& stream_instruction,
                  inum_t instruction_id,
                  inum_t prior_instruction_id,
                  ArchState state,
                  std::optional<SimulationControl> control,
                  DecoderMode decoder_mode = DecoderModeDefault);

    ProgramCounter program_counter() const override { return pc; };
    PhysAddr program_counter_pa() const override;
    uint32_t opcode() const override { return stream_instruction.opcode(); }
    bool has_exception() const override { return stream_instruction.has_exception(); }

    VirtAddr virtual_address(unsigned n = 0) const override;
    uint32_t num_addresses() const override;
    PhysAddr physical_address(unsigned n = 0) const override;
    bool memop_predicated_off(unsigned n = 0) const override;
    std::uint32_t memop_bytes(unsigned n = 0) const override; // number of memory bytes referenced
    ProgramCounter next_program_counter() const override { return next_pc; }
    inum_t inum() const override { return instruction_id; }
    inum_t prev_inum() const override { return prior_instruction_id; }

    bool wrong_path() const override { return instruction_id.wrong_path(); }
    bool artificial_encoding() const override { return _artificial_encoding; }
    bool emulated() const override { return is_emulated; }
    void set_emulated() { is_emulated = true; }
    void set_emulated_br_taken() { emulated_br_taken = true; }
    void add_emulated_virt_addr(VirtAddr va) { emulated_virt_addr.emplace_back(va); }
    void set_next_pc(ProgramCounter pc) { next_pc = pc; }

    UopSet decode(DecoderMode decoder_mode = DecoderModeDefault) override;
    void execute() override;
    void set_state(ArchState state) { arch_state = state; }
    ArchState state() const override { return arch_state; }
    const ArchState& state_ref() const { return arch_state; }
    std::uint32_t pstate_nzcv() const { return static_cast<std::uint32_t>(arch_state.read(NZCV)); }
    std::optional<SimulationControl> control() override { return simulation_control; };

    std::string disasm() const override { return adis::disasm(opcode(), adis_info()); }

private:
    void set_opcode(std::uint32_t opcode);
    static ampere::is::Features stream_features;

    VirtAddr zva_address() const;
    PhysAddr virt_to_phys(unsigned n = 0) const;

    ampere::is::Instruction stream_instruction;
    ProgramCounter pc{0xDEADC0DE};
    ProgramCounter next_pc{0xDEADC0DE};
    inum_t instruction_id;
    inum_t prior_instruction_id;
    ArchState arch_state; // input, set up by driver

    std::optional<SimulationControl> simulation_control{std::nullopt};

    VirtAddr use_emulated_address(unsigned n) const;

    bool is_emulated{false};
    bool emulated_br_taken{false};
    bool _artificial_encoding{false};
    std::vector<VirtAddr> emulated_virt_addr;
    std::set<archx::LReg> modified_regs;
    friend class DriverIS;

public:
    ArchState arch_state_output; // computed by emulation

    void mod_reg(archx::LReg reg) { modified_regs.insert(reg); }
    void write_gpr32(archx::LReg dst, std::uint32_t value)
    {
        arch_state_output = std::move(arch_state_output).write(dst, value);
        mod_reg(dst);
    }
    void write_gpr64(archx::LReg dst, std::uint64_t value)
    {
        arch_state_output = std::move(arch_state_output).write(dst, value);
        mod_reg(dst);
    }
    void write_vfp(archx::LReg dst, __uint128_t value)
    {
        arch_state_output = std::move(arch_state_output).write_vfp(dst, value);
        mod_reg(dst);
    }
    void write_vfp(archx::LReg dst, std::array<uint64_t, 2> value)
    {
        arch_state_output = std::move(arch_state_output).write_vfp(dst, value);
        mod_reg(dst);
    }
    void write_flags(std::uint64_t value)
    {
        arch_state_output = std::move(arch_state_output).write(NZCV, value);
        mod_reg(archx::LReg::FLAGS);
    }
};

} // namespace ampere::archx

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
