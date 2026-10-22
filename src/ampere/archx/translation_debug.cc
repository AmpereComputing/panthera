//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/archx/translation_debug.h"
#include "ampere/archx/translation.h"

#include <fmt/format.h>

namespace ampere {
namespace archx {

std::string debug_translation(const DriverPort& driver, VirtAddr va, bool is_instr)
{
    std::string output = fmt::format("Debugging translation for VA {0:#x}:\n", va.value());
    Bitfield tcr_el1 = Bitfield{driver->read_sysreg(TCR_EL1)};
    Bitfield ttbr0 = Bitfield{driver->read_sysreg(TTBR0_EL1)};
    Bitfield ttbr1 = Bitfield{driver->read_sysreg(TTBR1_EL1)};
    auto asid = driver->asid();

    output += fmt::format("TCR_EL1: {0:#x}, TTBR0_EL1: {1:#x}, TTBR1_EL1: {2:#x}, ASID: {3}, EL: {4}\n",
                          tcr_el1.value(),
                          ttbr0.value(),
                          ttbr1.value(),
                          asid,
                          static_cast<int>(driver->exception_level()));

    auto tr = Translation(va, tcr_el1, ttbr0, ttbr1, is_instr);
    while (!tr.done()) {
        auto next_pte = tr.next_pte();
        uint64_t next_pte_contents = driver->read_memory(next_pte);
        output += fmt::format("PTE contents at {0:#x}: {1:#x}\n", next_pte.value(), next_pte_contents);
        tr.pte_fetched(next_pte_contents);
    }
    if (tr.faulted()) {
        output += fmt::format("Translation for VA {0:#x} faulted\n", va.value());
    } else {
        auto [pa, page_size] = tr.result();
        output += fmt::format("Translation for VA {0:#x} resulted in PA {1:#x} ({2} page starting at {3:#x})\n",
                              va.value(),
                              tr.result_address().value(),
                              std::string(page_size),
                              pa.value());
    }
    return output;
}

} // namespace archx
} // namespace ampere

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
