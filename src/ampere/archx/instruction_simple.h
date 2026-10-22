//
// Copyright (c) 2018-2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#ifndef AMPERE_ARCHX_INSTRUCTION_SIMPLE_H
#define AMPERE_ARCHX_INSTRUCTION_SIMPLE_H

#include <boost/smart_ptr/make_unique.hpp>
#include "ampere/archx/instruction.h"
#include "ampere/archx/furiosa.h"

namespace ampere {
namespace archx {

const uint32_t nop_opcode = {0xd503201f};

class InstructionSimple : public Instruction
{
public:
    InstructionSimple() = default;

    InstructionSimple(ProgramCounter pc) : m_program_counter(pc), m_program_counter_pa{pc.value()} {}

    InstructionSimple(ProgramCounter pc, uint32_t opcode)
        : m_program_counter(pc), m_program_counter_pa{pc.value()}, m_opcode(opcode)
    {
    }

    InstructionSimple(ProgramCounter pc, uint32_t opcode, ProgramCounter next_pc)
        : m_program_counter(pc), m_program_counter_pa{pc.value()}, m_next_program_counter(next_pc), m_opcode(opcode)
    {
    }

    InstructionSimple(ProgramCounter pc, uint32_t opcode, VirtAddr va)
        : m_program_counter(pc),
          m_program_counter_pa{pc.value()},
          m_opcode(opcode),
          m_virtual_address(va),
          m_physical_address{va.value()}
    {
    }

    ProgramCounter program_counter() const override { return m_program_counter; }
    PhysAddr program_counter_pa() const override { return m_program_counter_pa; }
    ProgramCounter next_program_counter() const override { return m_next_program_counter; };
    uint32_t opcode() const override { return m_opcode; }
    bool has_exception() const override { return false; }
    VirtAddr virtual_address(unsigned n = 0) const override
    {
        return m_virtual_address;
        (void)n;
    }
    uint32_t num_addresses() const override { return 1; }
    PhysAddr physical_address(unsigned n = 0) const override
    {
        return m_physical_address;
        (void)n;
    }
    bool memop_predicated_off(unsigned n = 0) const override
    {
        return false;
        (void)n;
    }
    std::uint32_t memop_bytes(unsigned n = 0) const override
    {
        return 0;
        (void)n;
    }
    inum_t inum() const override { return inum_t(1); }
    inum_t prev_inum() const override { return inum_t(0); }
    bool wrong_path() const override { return false; }
    bool artificial_encoding() const override { return false; }
    bool emulated() const override { return false; }

    void execute() override { throw std::runtime_error("execute() method not implemented for InstructionSimple"); }

    ArchState state() const override { return m_state; }

    std::optional<SimulationControl> control() override { return std::nullopt; };

    void set_program_counter(ProgramCounter pc) { m_program_counter = pc; }
    void set_program_counter_pa(PhysAddr pa) { m_program_counter_pa = pa; }
    void set_next_pc(ProgramCounter pc) { m_next_program_counter = pc; }
    void set_opcode(uint32_t opcode) { m_opcode = opcode; }
    void set_virtual_address(VirtAddr va) { m_virtual_address = va; }
    void set_physical_address(PhysAddr pa) { m_physical_address = pa; }
    void set_id(uint64_t id) { m_id = id; }

    static UopUarch get_decoder_uarch() { return decoder_uarch; }
    static void set_decoder_uarch(UopUarch uarch) { decoder_uarch = uarch; }

    std::string disasm() const override { return "unknown"; }

private:
    ProgramCounter m_program_counter{0};
    PhysAddr m_program_counter_pa{0};
    ProgramCounter m_next_program_counter{m_program_counter + 4};
    uint32_t m_opcode{nop_opcode};
    VirtAddr m_virtual_address{0};
    PhysAddr m_physical_address{0};
    uint64_t m_id{1};
    ArchState m_state;

    static inline UopUarch decoder_uarch = UopUarch::UNKNOWN;
};

} // namespace archx
} // namespace ampere

#endif // AMPERE_ARCHX_INSTRUCTION_SIMPLE_H

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
