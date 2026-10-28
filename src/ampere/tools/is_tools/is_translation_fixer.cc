//
// Copyright (c) 2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

/*
 * This tool will take a version-3 instruction stream and attempt to
 * re-translate any physical addresses which are 0. If the memory access
 * faulted or if the necessary PTEs are not yet present in the instruction
 * stream before the instruction being processed, there will be no change. If
 * the translation yields a valid PA, it will be placed in the instruction
 * stream instead of the original 0. This tool is made primarily to fix up
 * instruction streams created with the tracing framework before the final fix
 * for this issue is completed.
 */

#include <fcntl.h>
#include <iostream>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "ampere/instruction_stream/instruction_stream.h"
#include "ampere/archx/instruction_is.h"
#include "ampere/archx/addr.h"
#include "ampere/archx/sysreg.h"
#include "ampere/archx/translation.h"
#include "ampere/archx/uop_info.h"

using namespace ampere;
using namespace ampere::is;

int main(int argc, char** argv)
{
    if (argc != 3) {
        std::cerr << "is_translation_fixer INPUT_STREAM OUTPUT_STREAM" << std::endl;
        exit(1);
    }

    archx::ArchState translation_state = archx::ArchState();

    InstructionStreamReader reader(argv[1]);
    InstructionStreamWriter writer(argv[2], reader.features());
    Event event;

    while (reader.read(event)) {
        if (event.has_memory()) {
            const Memory& memory = event.memory();
            // memory.address().pa()
            auto pa = archx::PhysAddr(memory.address().pa());
            translation_state =
                std::move(translation_state).write(pa, (uint8_t*)memory.value().data(), memory.value().size());
        } else if (event.has_sysreg()) {
            const SysReg& s = event.sysreg();
            archx::SysReg sysreg = archx::SysReg(s.op0(), s.op1(), s.crn(), s.crm(), s.op2());
            translation_state = std::move(translation_state).write(sysreg, s.value());
        }
        if (event.has_instruction()) {
            Instruction instruction = event.instruction();
            if (instruction.memop_size() > 0) {
                bool fixed = false;
                for (int i = 0; i < instruction.memop_size(); i++) {
                    auto pa = archx::PhysAddr(instruction.memop(i).pa());
                    auto va = archx::VirtAddr(instruction.memop(i).va());

                    // Check if VA is nonzero and PA is zero. If so, attempt
                    // re-translation. If that returns with a nonzero PA, write
                    // that one to the instruction stream instead of 0.
                    if (va.value() != 0 && pa.value() == 0x0) {
                        auto tcr_el1 = archx::Bitfield(translation_state.read(archx::TCR_EL1));
                        auto ttbr0_el1 = archx::Bitfield(translation_state.read(archx::TTBR0_EL1));
                        auto ttbr1_el1 = archx::Bitfield(translation_state.read(archx::TTBR1_EL1));
                        auto tr = archx::Translation(va, tcr_el1, ttbr0_el1, ttbr1_el1, false);
                        // perform the table walk
                        while (!tr.done()) {
                            tr.pte_fetched(translation_state.read(tr.next_pte()));
                        }
                        if (!tr.faulted()) {
                            instruction.mutable_memop(i)->set_pa(tr.result_address().value());
                            fixed = true;
                        }
                    }
                }
                if (fixed)
                    event.mutable_instruction()->CopyFrom(instruction);
            }
        }
        writer.write(event);
        event.Clear();
    }
}

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
