//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/core/l2/crp/oracle.h"

namespace ampere::core {

L2Oracle::L2Oracle(const sc_core::sc_module_name& name) : implements<L2Oracle, L2RRIP>(name) {}

void L2Oracle::oracle_add_pa(PhysAddr pa, uint64_t inum)
{
    auto& mem = oracle_memory[pa.line_address()];

    if (mem.empty())
        ev_l2_oracle_new_line();
    else
        ev_l2_oracle_reuse_line();

    if (mem.empty() || mem.back() != inum)
        mem.push_back(inum);
}

void L2Oracle::scan_memory_stream()
{
    ms::MemoryStreamReader ms_reader(memory_stream_name.c_str());
    ms::Access ms_access;
    ms::Event ms_event;

    while (ms_reader.read(ms_event)) {
        if (ms_event.has_control()) {
            ms::Control control = ms_event.control();
            if (control.type() == ms::Control_Type_END_MEMORY)
                break;
            continue;
        }

        AMPERE_ASSERT(ms_event.has_access(), "Memory event has no access");
        ms_access.Swap(ms_event.mutable_access());

        if ((++warmup_access_count & 0xfffff) == 0)
            printf("L2 oracle warmup: access %lu\n", warmup_access_count);

        oracle_add_pa(PhysAddr{ms_access.pa()}, warmup_access_count);
    }
}

void L2Oracle::scan_instruction_stream()
{
    uint64_t inum{0};
    uint64_t start_measure{0};

    while (true) {
        // A few traces do not have a stop_measurement marker, so we have to catch the exception
        // generated when we try to read past the end of the trace.
        std::shared_ptr<Instruction> archx_instruction;
        try {
            auto next_pc = oracle_driver->next_pc();
            archx_instruction = oracle_driver->fetch(next_pc);
            ++inum;
        } catch (std::runtime_error&) {
            break;
        }

        if ((inum & 0xfffff) == 0)
            printf("L2 oracle init: instruction %lu\n", inum);

        // Record this instruction as accessing the PA of the instruction itself
        oracle_add_pa(archx_instruction->program_counter_pa(), inum + warmup_access_count);

        // Now record any data PAs accessed by this instruction
        auto phys_addrs = archx_instruction->num_addresses();
        for (uint64_t pa_index = 0; pa_index < phys_addrs; pa_index++)
            oracle_add_pa(archx_instruction->physical_address(pa_index), inum + warmup_access_count);

        if (archx_instruction->control() == archx::SimulationControl::STOP_MEASUREMENT)
            break;

        // Some traces are ridiculously long -- cap at 20M measurement instructions to avoid running out of memory
        if (archx_instruction->control() == archx::SimulationControl::START_MEASUREMENT)
            start_measure = inum;
        if (start_measure && inum - start_measure > 20000000)
            break;

        oracle_driver->commit(archx_instruction);
    }
}

void L2Oracle::initialize()
{
    if (oracle_warmup && !memory_stream_name.empty())
        scan_memory_stream();
    printf("L2 oracle: after warmup, %lu unique physical addresses\n", oracle_memory.size());

    if (oracle_trace)
        scan_instruction_stream();
    printf("L2 oracle: after instruction stream, %lu unique physical addresses\n", oracle_memory.size());

    initialized = true;
}

uint64_t L2Oracle::next_reference(PhysAddr addr)
{
    AMPERE_ASSERT(addr == addr.line_address(), "L2 Oracle saw misaligned address");

    auto it = oracle_memory.find(addr);
    uint64_t current_id;

    // During warmup, the ID of the access is the number of times touch() has been called (once per access in the
    // memory stream). Once simulation starts, the ID is the last committed instruction, offset by the number of
    // warmup accesses.
    if (get_cycle() == 0)
        current_id = touch_count;
    else
        current_id = real_driver->committed_id() + warmup_access_count;

    if (it == oracle_memory.end()) {
        // this line is never referenced by any instruction -- the perfect victim
        ev_l2_oracle_never_used();
        return 0;
    }

    // If there is a pending write to this line in the PWC, don't choose it as a victim -- act as though it is
    // referenced by the very next instruction
    if (pwc->has_address_match(addr)) {
        ev_l2_oracle_pending_write();
        return current_id + 1;
    }

    // Now check for a pending write in the FIFO between LSU and PWC
    auto fifo_stores = (*lsu_write_request)->unsafe_size();
    for (uint64_t i = 0; i < fifo_stores; i++) {
        if ((*lsu_write_request)->unsafe_peek(i)->phys_addr.same_cache_line(addr)) {
            ev_l2_oracle_pending_write();
            return current_id + 1;
        }
    }

    auto inst_ids = it->second;
    auto next_ref = std::lower_bound(inst_ids.begin(), inst_ids.end(), current_id);

    if (next_ref == inst_ids.end()) {
        // this line is not referenced again
        ev_l2_oracle_not_again();
        return 0;
    }

    return *next_ref;
}

// FIXME: should this function take ways_mask into account?
void L2Oracle::touch(
    uint64_t bank, uint64_t set, uint64_t way, TouchOp op, bool demand, const Set& set_contents, MemReq req)
{
    // We do initialization on the first call instead of end_of_elaboration so we are set up before L2 warmstart
    if (!initialized)
        initialize();

    ++touch_count;

    // If the oracle is being used for victim selection (instead of RRIP prediction), then we just do normal RRIP
    // updates
    if (!predictor_mode) {
        L2RRIP::touch(bank, set, way, op, demand, set_contents, req);
        return;
    }

    touch_stats(bank, set, way, op, demand, req);

    // The first hit on a prefetched line may be treated like a fill
    if (op == TouchOp::REFILL || (pf_hit_like_fill && set_contents[way].line.prefetch)) {
        if (demand || prefetch_fills_act_like_demand) {
            auto next_ref = next_reference(req->phys_addr.line_address());
            auto new_age = next_ref ? rrip_max - insert_delta : rrip_max;
            set_age(bank, set, way, op, new_age);
        } else
            set_age(bank, set, way, op, rrip_max);
    } else if (op == TouchOp::LOOKUP) {
        set_age(bank, set, way, op, 0);
    } else if (op == TouchOp::UPDATE && write_hit_updates_age) {
        set_age(bank, set, way, op, 0);
    }
}

// Choose a victim for replacement
uint64_t L2Oracle::victim(uint64_t bank, uint64_t set, const Set& set_contents, uint64_t way_mask)
{
    // With oracle prediction, we perform victim selection as usual and the oracle is used to choose the RRIP age on
    // fill
    if (oracle_memory.empty() || predictor_mode)
        return L2RRIP::victim(bank, set, set_contents, way_mask);

    // First check for an invalid (non-busy) way
    for (auto w = 0u; w < ways; ++w) {
        if (set_contents[w].line.busy)
            continue;
        if (set_contents[w].line.state == LineState::I)
            return w;
    }

    uint64_t farthest_reuse{0};
    uint64_t farthest_way{0};
    for (auto w = 0u; w < ways; ++w) {
        if (set_contents[w].line.busy)
            continue;

        auto next_ref = next_reference(set_contents[w].line.addr);
        if (next_ref == 0)
            return w;

        if (next_ref > farthest_reuse) {
            farthest_reuse = next_ref;
            farthest_way = w;
        }
    }

    AMPERE_ASSERT(farthest_reuse != 0, "L2 oracle failed to find a victim");
    return farthest_way;
}

} // namespace ampere::core
