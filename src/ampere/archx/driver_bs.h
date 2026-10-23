//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

// DriverBS implements the Driver interface in a very simplistic way based on
// the contents of a branch stream. It presents realistic branch instructions
// containing correct PC's (virtual-only), well-formed opcodes, taken/not taken
// and branch target information. In between branches, however, all
// instructions are `add x1, x1, x1`. DriverBS was created to drive bobcat for
// BPU warmup, and in general is only intended to be used to drive models which
// need only this limited branch information.

#pragma once

#include "ampere/archx/addr.h"
#include "ampere/archx/driver.h"
#include "ampere/archx/instruction_is.h"
#include "ampere/archx/arch_state.h"
#include "ampere/archx/translation.h"
#include "ampere/branch_stream/branch_stream.h"

#include <elfio/elfio.hpp>
#include <queue>

namespace ampere::archx {

class DriverBS : public Driver
{
public:
    explicit DriverBS(std::string branch_stream_name);
    ProgramCounter next_pc(bool* is_taken_branch = nullptr) override;
    std::shared_ptr<Instruction> fetch(ProgramCounter pc) override;
    void commit(std::shared_ptr<Instruction> instruction) override;
    void recover(inum_t id) override;
    uint64_t read_memory(PhysAddr addr) const override;
    uint64_t read_sysreg(SysReg addr) const override;
    EL exception_level() const override;
    uint16_t asid() const override;
    uint64_t committed_id() const override;

private:
    void initialize_first_instruction();
    void record_event(const bs::Event& event, InstructionIS& prev);
    InstructionIS read_branch_stream(InstructionIS& prev);
    void read_next_instruction();
    Translation translate(VirtAddr va, bool is_instr);
    Translation translate(ProgramCounter pc);
    std::string branch_stream_name;
    bs::BranchStreamReader branch_stream;
    std::map<int64_t, std::shared_ptr<InstructionIS>> instructions;
    uint64_t sequence_id{0};
    uint64_t next_pc_id{1};
    uint64_t fetch_id{1};
    InstructionIS next_instruction;
    InstructionIS next_branch;
    bool on_wrong_path{false};
    bool fetch_state_updated{false};
    bool stopping{false};
    ArchState fetch_state, arch_state;
};

} // namespace ampere::archx

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
