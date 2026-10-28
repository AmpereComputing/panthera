//
// Copyright (c) 2022-2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <optional>

#include <fmt/format.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>

#include "ampere/branch_stream/branch_stream.pb.h"
#include "ampere/protobuf_utils/protobuf_utils.h"
#include "ampere/archx/driver_bs.h"

namespace ampere {
namespace archx {

DriverBS::DriverBS(std::string branch_stream_name)
    : branch_stream_name{branch_stream_name}, branch_stream(branch_stream_name)
{
}

ProgramCounter DriverBS::next_pc(bool* is_taken_branch)
{
    if (!instructions.contains(next_pc_id))
        read_next_instruction();
    if (is_taken_branch)
        *is_taken_branch = instructions.at(next_pc_id - 1)->is_taken_branch();
    return instructions.at(next_pc_id++)->program_counter();
}

Translation DriverBS::translate(VirtAddr va [[maybe_unused]], bool is_instr [[maybe_unused]] = false)
{
    throw std::runtime_error{"BS-ing... address translation unavailable"};
}

Translation DriverBS::translate(ProgramCounter pc)
{
    return translate(VirtAddr{pc.value()}, /*is_instr=*/true);
}

std::shared_ptr<Instruction> DriverBS::fetch(ProgramCounter pc)
{
    if (on_wrong_path) {
        return std::make_shared<InstructionIS>(pc);
    }

    if (fetch_id >= sequence_id)
        read_next_instruction();

    auto fetch_instruction = instructions.at(fetch_id);

    if (pc == fetch_instruction->program_counter()) {
        fetch_id++;
        return fetch_instruction;
    }

    on_wrong_path = true;

    return std::make_shared<InstructionIS>(pc);
}

void DriverBS::commit(std::shared_ptr<Instruction> instruction)
{
    if (instructions.empty())
        throw std::runtime_error("Commit called with an empty instructions buffer.");
    const auto& begin = instructions.begin();
    if (instruction == begin->second) {
        arch_state = instruction->state();
        instructions.erase(begin);
    } else {
        constexpr auto msg = "Driver commit error expected instruction {} got {}";
        throw std::runtime_error(fmt::format(msg, begin->second->inum().id(), instruction->inum().id()));
    }
}

void DriverBS::initialize_first_instruction()
{
    assert(sequence_id == 0);

    InstructionIS bogus = InstructionIS(0x0_pc);
    next_branch = InstructionIS(0x0_pc);
    next_instruction = read_branch_stream(bogus);
    next_instruction = read_branch_stream(next_instruction);

    /* sync fetch_state to arch_state before starting to execute to ensure the
     * initial state is correct. */
    arch_state = fetch_state;
}

void DriverBS::read_next_instruction()
{
    if (instructions.size() > 10000)
        throw std::runtime_error("Exceeded maximum number of uncommitted instructions");

    // Initialize first instruction if we have not yet done so
    if (sequence_id == 0)
        initialize_first_instruction();

    auto current_instruction = std::move(next_instruction);

    next_instruction = read_branch_stream(current_instruction);

    auto instruction = std::make_shared<InstructionIS>(current_instruction);
    instructions.emplace(instruction->inum().id(), instruction);
}

/* Check if event needs to update architectural state (memory, sysregs, etc.),
 * and record any needed updates. This requires that the previous instruction
 * be passed in because memory updates must be recorded for its state. */
void DriverBS::record_event(const bs::Event& event, InstructionIS& prev)
{
    if (event.has_sysreg()) {
        const is::SysReg& s = event.sysreg();
        SysReg sysreg = SysReg(s.op0(), s.op1(), s.crn(), s.crm(), s.op2());
        fetch_state = std::move(fetch_state).write(sysreg, s.value());
        fetch_state_updated = true;
    } else if (event.has_context()) {
        const is::Context& c = event.context();
        if (c.type() == is::Context::EL) {
            /* Because EL changes are immediately before the instruction which
             * needs to observe the change in the instruction stream, and will
             * therefore only be observed *after* the instruction which needs
             * them commits, add EL updates to the state for the preceeding
             * instruction, too. */
            prev.set_state(std::move(prev.state()).set_exception_level(static_cast<EL>(c.value())));

            /* As a memory optimization, re-use the same state for the current
             * fetch_state if no updates have occurred since the last
             * instruction's state */
            if (!fetch_state_updated) {
                fetch_state = prev.state();
            } else {
                fetch_state = std::move(fetch_state).set_exception_level(static_cast<EL>(c.value()));
            }
        }
    }
}

InstructionIS DriverBS::read_branch_stream(InstructionIS& prev)
{
    // Return add instructions until we reach the next branch in the stream
    if (sequence_id != 0 && prev.next_program_counter() < next_branch.program_counter()) {
        // opcode 0x91002c21 is for add x1, x1, x1
        inum_t prior_id(sequence_id);
        inum_t current_id(++sequence_id);
        return InstructionIS(prev.next_program_counter(), 0x91002c21, current_id, prior_id, fetch_state);
    }

    bs::Event event;
    bool status = branch_stream.read(event);
    bool el_change = false;
    while (status && !event.has_branch()) {
        if (event.has_context()) {
            const is::Context& c = event.context();
            if (c.type() == is::Context::EL) {
                // Set the 'next pc' for the current instruction to the next
                // branch instead of PC+4
                el_change = true;
            }
        }
        record_event(event, prev);
        event.Clear();
        status = branch_stream.read(event);
    }

    std::optional<SimulationControl> simulation_control;
    auto next_sequence_id = sequence_id + 1;
    if (sequence_id == 0) {
        simulation_control = SimulationControl::START_MEASUREMENT;
        // If sequence_id is 0 and the program counter of the "next branch" is
        // also 0, assume that we are setting up and avoid incrementing the
        // sequence_id
        if (next_branch.program_counter() == 0x0_pc)
            next_sequence_id = sequence_id;
    }
    inum_t prior_id(sequence_id);
    inum_t current_id(next_sequence_id);
    sequence_id = next_sequence_id;

    InstructionIS this_branch = InstructionIS(next_branch.program_counter(),
                                              next_branch.opcode(),
                                              current_id,
                                              prior_id,
                                              next_branch.state(),
                                              next_branch.control());
    this_branch.set_next_pc(next_branch.next_program_counter());

    if (status) {
        // Create an instruction from the next branch in the stream
        bs::Branch branch = event.branch();

        // Manufacture an opcode matching the type of the branch (and target if
        // a direct branch)
        uint32_t opcode = 0;
        int64_t target_offset = 4;
        if (branch.taken())
            target_offset = branch.target() - branch.pc();
        switch (protobuf_to_branchtype(branch.type())) {
        case BranchType::CONDITIONAL_DIRECT: // b.lt #imm19
            // If the target offset lies outside the possible range for
            // this encoding, it probably means the trace saw an exception
            // immediately following the execution of this branch. In this
            // case, just make up a target.
            if (target_offset <= -(1L << 20) || target_offset >= (1L << 20))
                target_offset = -16;

            opcode = 0x5400000b | (((target_offset >> 2) & 0x7ffff) << 5);
            break;
        case BranchType::CALL_DIRECT: // bl #imm26
            opcode = 0x80000000;
            [[fallthrough]];
            // fall through (bit 31, the "call hint" is only difference)
        case BranchType::UNCONDITIONAL_DIRECT: // b #imm26
            // If the target offset lies outside the possible range for
            // this encoding, it probably means the trace saw an exception
            // immediately following the execution of this branch. In this
            // case, just make up a target.
            if (target_offset <= -(1L << 27) || target_offset >= (1L << 27))
                target_offset = -16;

            opcode |= 0x14000000 | ((target_offset >> 2) & 0x3ffffff);
            break;
        case BranchType::UNCONDITIONAL_INDIRECT: // br x1
            opcode = 0xd61f0020;
            break;
        case BranchType::CALL_INDIRECT: // blr x1
            opcode = 0xd63f0020;
            break;
        case BranchType::RETURN: // ret x30
            opcode = 0xd65f0000;
            break;
        case BranchType::NONE:
        case BranchType::UNSPECIFIED:
        case BranchType::ILLEGAL:
            throw std::runtime_error("Encountered unexpected branch type in branch stream\n");
        }

        inum_t prior_id(0);
        inum_t current_id(0);
        next_branch = InstructionIS(ProgramCounter(branch.pc()),
                                    opcode,
                                    current_id /*will be replaced*/,
                                    prior_id,
                                    fetch_state,
                                    simulation_control);
        if (branch.taken())
            // Constructor defaults to setting next program counter to PC+4, set to
            // target if taken
            next_branch.set_next_pc(ProgramCounter(branch.target()));
    } else {
        // If we didn't find a branch in the branch stream, insert
        // STOP_MEASUREMENT maker and begin making up instructions in a
        // straight line forever
        if (!stopping) {
            simulation_control = SimulationControl::STOP_MEASUREMENT;
            stopping = true;
        }

        inum_t prior_id(0);
        inum_t current_id(0);
        // opcode 0x91002c21 is for add x1, x1, x1
        next_branch = InstructionIS(0xfffffffffffffffc_pc,
                                    0x91002c21,
                                    current_id /*will be replaced*/,
                                    prior_id,
                                    fetch_state,
                                    simulation_control);
    }

    // If we saw an EL change or an unexpected discontinuity (backwards PC or
    // PC+4k) in the dynamic instruction stream since the previous branch, set
    // "next program counter" of the previous branch to the next branch
    if (el_change || next_branch.program_counter() < this_branch.next_program_counter()
        || next_branch.program_counter() > this_branch.next_program_counter() + 4096)
    {
        this_branch.set_next_pc(next_branch.program_counter());
    }

    fetch_state_updated = false;
    event.Clear();
    return this_branch;
}

void DriverBS::recover(inum_t id)
{
    inum_t next_id(id.id() + 1);

    if (next_id == next_instruction.inum())
        read_next_instruction();

    if (instructions.contains(next_id.id())) {
        fetch_id = next_id.id();
        next_pc_id = next_id.id();
        on_wrong_path = false;
    } else {
        auto msg = fmt::format("Instruction ID {} does not exist in DriverBS", next_id.id());
        throw std::runtime_error(msg);
    }
}

uint64_t DriverBS::read_memory(const PhysAddr addr) const
{
    return arch_state.read(addr);
}

uint64_t DriverBS::read_sysreg(const SysReg sysreg) const
{
    return arch_state.read(sysreg);
}

EL DriverBS::exception_level() const
{
    return arch_state.exception_level();
}

uint16_t DriverBS::asid() const
{
    Bitfield tcr_el1 = Bitfield{read_sysreg(TCR_EL1)};

    /* TCR_EL1.A1 selects whether the ASID resides in TTBR0_EL1 or TTBR1_EL1 */
    if (tcr_el1.extract_range(22, 22)) {
        return (uint16_t)Bitfield{read_sysreg(TTBR1_EL1)}.extract_range(63, 48);
    } else {
        return (uint16_t)Bitfield{read_sysreg(TTBR0_EL1)}.extract_range(63, 48);
    }
}

uint64_t DriverBS::committed_id() const
{
    return 0;
}

} // namespace archx
} // namespace ampere

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
