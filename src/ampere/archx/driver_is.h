//
// Copyright (c) 2019-2024 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#pragma once
#include <queue>
#include <optional>
#include "ampere/archx/addr.h"
#include "ampere/archx/driver.h"
#include "ampere/archx/instruction_is.h"
#include "ampere/archx/inum.h"
#include "ampere/archx/arch_state.h"
#include "ampere/archx/translation.h"
#include "ampere/instruction_stream/instruction_stream.h"

#include <elfio/elfio.hpp>

namespace ampere::archx {

class DriverIS : public Driver
{
public:
    explicit DriverIS(std::string instruction_stream_name);
    DriverIS(std::string instruction_stream_name,
             uint64_t skip_instructions,
             bool speculative_memory_image,
             bool arg_use_wrong_path_emulation,
             std::uint64_t arg_validate_emulation,
             std::string mem_image_path);
    ProgramCounter next_pc(bool* is_taken_branch = nullptr) override;
    std::shared_ptr<Instruction> fetch(ProgramCounter pc) override;
    void commit(std::shared_ptr<Instruction> instruction) override;
    void recover(inum_t id) override;
    uint64_t read_memory(PhysAddr addr) const override;
    uint64_t read_sysreg(SysReg addr) const override;
    EL exception_level() const override;
    uint16_t asid() const override;
    uint64_t committed_id() const override;
    uint64_t emulation_divergences{0};
    uint64_t validation(const std::string& key [[maybe_unused]]) const override;

private:
    bool has_regs{false};
    bool has_flags{false};
    bool has_prefetch_regs{false};

    void initialize_first_instruction();
    void record_event(const is::Event& event, InstructionIS& prev);
    InstructionIS read_instruction_stream(InstructionIS& prev);
    void read_next_instruction();
    Translation translate(VirtAddr va, bool is_instr);
    Translation translate(ProgramCounter pc);
    ArchState load_memory_image(const std::string& instruction_stream_name);
    ArchState load_elf(ELFIO::elfio& reader);
    std::shared_ptr<Instruction> fetch_wrong_path(ProgramCounter pc);
    void read_src_regs_from_trace(const is::Instruction& strm_inst);
    std::pair<ArchState, bool> read_dst_regs_from_trace(ArchState state, const is::Instruction& strm_inst);
    void validate_one_emulation(const is::Instruction& strm_inst, const InstructionIS& sim_inst);
    void validate_emulation_memops(const is::Instruction& strm_inst, const InstructionIS& sim_inst);
    void validate_emulation_dest_regs(const is::Instruction& strm_inst, const InstructionIS& sim_inst);
    void dump_trace_updates_for_debugging(const is::Instruction& strm_inst, const InstructionIS& sim_inst) const;
    void recover_wrong_path(inum_t id);
    void recover_using_trace(inum_t id);
    void call_emulation(InstructionIS& instr);

    uint64_t skip_instructions;
    bool speculative_memory_image;
    bool use_wrong_path_emulation;
    // 0=off, 1=count divergences, 2=emit each instr & divergences, 3=trace omitted values, 4=everything
    std::uint64_t validate_emulation{0};
    std::string mem_image_path;

    std::string instruction_stream_name;
    is::InstructionStreamReader instruction_stream;
    std::map<int64_t, std::shared_ptr<InstructionIS>> instructions;
    std::map<int64_t, std::shared_ptr<InstructionIS>> wrong_path_instructions;
    uint64_t sequence_id{0};
    int64_t wrong_path_sequence_id{0};
    uint64_t last_committed_id{0};
    uint64_t next_pc_id{1};
    uint64_t fetch_id{1};
    InstructionIS next_instruction;
    bool on_wrong_path{false};
    bool loaded_memory_image{false};
    ArchState fetch_state, arch_state;

    // prev_stream_instruction is used for extracting dest reg state from
    // previous instruction to augument the incoming register state
    // for the current instr.
    std::optional<is::Instruction> prev_stream_instruction{std::nullopt};
    std::optional<std::shared_ptr<InstructionIS>> prev_wrong_path_instruction{std::nullopt};
    inum_t prev_correct_path_id;
};

} // namespace ampere::archx

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
