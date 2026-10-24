//
// Copyright (c) 2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include "ampere/archx/addr.h"
#include "ampere/archx/instruction_is.h"
#include "ampere/archx/translation.h"

namespace ampere::tigris {

static inline bool is_memory_op_pair(const archx::Instruction& instruction)
{
    if (instruction.num_addresses() % 2 == 0 && !instruction.is_sve()
        && (instruction.is_load() || instruction.is_store()))
    {
        for (unsigned i = 0; i < instruction.num_addresses() / 2; ++i) {
            auto difference = instruction.virtual_address(i * 2 + 1) - instruction.virtual_address(i * 2);
            if (difference != 4 && difference != 8)
                return false;
        }
        return true;
    }
    return false;
}

static inline bool should_prefetch(const archx::Instruction& instruction, unsigned int index)
{
    return !(instruction.is_prefetch() || instruction.is_dc_zva() || instruction.is_dc_gzva());
}

static inline uint32_t memory_op_size(const archx::Instruction& instruction, unsigned index)
{
    if (!instruction.is_prefetch() && !instruction.is_dc_zva() && !instruction.is_dc_gzva()) {
        auto size = instruction.memop_bytes(index);
        if (size > 0)
            return size;

        if (is_memory_op_pair(instruction)) {
            auto difference = instruction.virtual_address(1) - instruction.virtual_address(0);
            return difference * 2;
        }
    }

    return 1;
}

static inline bool valid_memory_operation(const archx::Instruction& instruction, unsigned int index)
{
    if (instruction.is_prefetch() || instruction.is_dc_zva() || instruction.is_dc_gzva())
        return instruction.virtual_address(index) != archx::VirtAddr(0)
               && instruction.physical_address(index) != archx::PhysAddr(0);

    // FIXME: should prefetchers see these?
    if (instruction.memop_predicated_off(index))
        return false;
    // skip second in pair
    if (is_memory_op_pair(instruction) && index > 0)
        return false;
    // skip duplicate addresses in memop
    if (index > 0 && instruction.virtual_address(index) == instruction.virtual_address(0))
        return false;
    return true;
}

static inline ampere::archx::Translation translate(const archx::Instruction& instruction, archx::VirtAddr virt_addr)
{
    bool data = !instruction.is_prefetch();
    auto tr = ampere::archx::Translation(virt_addr,
                                         ampere::archx::Bitfield(instruction.state().read(archx::TCR_EL1)),
                                         ampere::archx::Bitfield(instruction.state().read(archx::TTBR0_EL1)),
                                         ampere::archx::Bitfield(instruction.state().read(archx::TTBR1_EL1)),
                                         !data);

    while (!tr.done()) {
        auto next_pte = tr.next_pte();
        uint64_t next_pte_contents = instruction.state().read(next_pte);
        tr.pte_fetched(next_pte_contents);
    }

    return tr;
}

static inline ampere::archx::PageSize::Size translate_page(const archx::Instruction& instruction,
                                                           archx::VirtAddr virt_addr)
{
    auto tr = translate(instruction, virt_addr);
    return tr.faulted() ? archx::PageSize::Size::PS_4KB : tr.result().second.value();
}

}; // namespace ampere::tigris
