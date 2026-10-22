//
// Copyright (c) 2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/core/perfect_fe.h"

namespace ampere::core {

using namespace uarch_common;

PerfectFE::PerfectFE(const sc_core::sc_module_name& name) : Module(name) {}

bool PerfectFE::run()
{
    read_throttling_counters();
    cache_snoop();
    tlb_invalidation();
    handle_bob();
    reclaim_credits();
    iq_stage();
    // Flush *after* writing instructions to ensure no (valid) instructions are
    // written in the same cycle as a flush because the IDR will eat them.
    handle_flush();

    return true;
}

void PerfectFE::handle_bob()
{
    while (bob_alloc->can_take()) {
        auto uop = bob_alloc->take();
        bob.emplace(uop->alloc_id, uop);
    }

    if (branches_committed->can_take()) {
        pending_commits += branches_committed->take();
    }

    int commits_this_cycle = 0;
    while (pending_commits && (!branch_commit_limit || commits_this_cycle < branch_commit_limit)) {
        auto& uop = bob.begin()->second;
        bob_credit_return->put(std::move(uop->bobid));
        bob.erase(bob.begin());
        commits_this_cycle++;
        pending_commits--;
    }
}

void PerfectFE::reclaim_credits()
{
    while (instruction_credit_return->can_take()) {
        auto credit = instruction_credit_return->take();
        release_credit(credit);
    }
    while (feq_credit_return->can_take()) {
        auto credit = feq_credit_return->take();
        release_credit(credit);
    }
}

void PerfectFE::tlb_invalidation()
{
    while (tlbi_request->can_take()) {
        tlbi_request->take();
    }
}

void PerfectFE::cache_snoop()
{
    while (snoop_port->can_take()) {
        snoop_port->take();
    }
}

void PerfectFE::read_throttling_counters()
{
    if (frontend_stall->can_take()) {
        frontend_stall->take();
    }
    if (instructions_committed->can_take()) {
        instructions_committed->take();
    }
}

Instruction PerfectFE::fetch_instruction()
{
    last_fetched_pc = driver->next_pc();
    auto fetch = make<Fetch>(last_fetched_pc, current_fetch_id++);
    std::shared_ptr<archx::Instruction> archx_instruction;
    archx_instruction = driver->fetch(last_fetched_pc);

    if (!archx_instruction)
        AMPERE_MODULE_THROW("Can't fetch instruction from archx driver");

    auto instruction = make_child<Instruction>(fetch, archx_instruction, fetch);
    instruction->fifoid = instruction_fifo.acquire();
    instruction->phys_addr = instruction->archx_instruction->program_counter_pa();

    // Ensure the instruction appears to have been perfectly predicted so the
    // XU tell the GPC we mispredicted, causing a flush
    if (instruction->archx_instruction->is_taken_branch())
        instruction->fetch->update_prediction({BranchType::UNSPECIFIED, last_fetched_pc, instruction->next_pc()});

    return instruction;
}

void PerfectFE::iq_stage()
{
    std::optional<ProgramCounter> last_pc;
    int taken_branches{0};

    while (iq_write_port->can_put() && instruction_fifo.can_acquire()) {
        auto instruction = fetch_instruction();
        auto current_pc = instruction->archx_instruction->program_counter();

        if (last_pc && fetch_alignment_bits > 0 && !current_pc.same_upper_bits(*last_pc, fetch_alignment_bits)) {
            release_credit(instruction->fifoid);
            driver->recover(instruction->prev_inum());
            break;
        }

        iq_write_event(instruction,
                       instruction->archx_instruction->program_counter(),
                       inst_bytes(instruction->archx_instruction->opcode()),
                       inst_id_data(instruction->archx_instruction->inum()),
                       fetch_id_data(instruction->fetch->fetch_id));

        iq_write_port->put(instruction);

        if (instruction->is_taken_branch())
            taken_branches++;
        if (taken_branches_per_cycle > 0 && taken_branches >= taken_branches_per_cycle) {
            break;
        }

        last_pc = current_pc;
    }
}

void PerfectFE::handle_flush()
{
    if (flush_port->can_read()) {
        auto flush_info = flush_port->read();
        auto instruction_id = flush_info->instruction->inum();
        // RAW scrub flushes will restart from 'scrubbed' instruction not next.
        if (flush_info->type == FlushType::RAW_SCRUB || flush_info->type == FlushType::CSF_NO_CMT)
            instruction_id = flush_info->instruction->prev_inum();
        driver->recover(instruction_id);
        bob_alloc->clear();
        flush_bob_entries(flush_info->alloc_id + 1);
    }
}

void PerfectFE::flush_bob_entries(std::uint64_t alloc_id)
{
    for (auto entry = bob.lower_bound(alloc_id); entry != bob.end();) {
        auto bobid = std::move(entry->second->bobid);
        release_credit(bobid);
        auto next_entry = entry;
        ++next_entry;
        bob.erase(entry);
        entry = next_entry;
    }
}

bool PerfectFE::address_resident(PhysAddr addr) const
{
    return false;
}

void PerfectFE::dump(std::ostream& os) const
{
    os << "= Perfect Frontend ======================================================\n";
    fmt::print("Last fetched PC: {:^#020x}\n", last_fetched_pc.value());
    os << "============================================================\n";
}

} // namespace ampere::core

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
