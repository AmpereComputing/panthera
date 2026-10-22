//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include <fcntl.h>
#include <iostream>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <cstdint>
#include "ampere/adis/src/adis.h"
#include "ampere/instruction_stream/instruction_stream.h"

using namespace ampere::is;

int main(int argc, char **argv)
{
    if (argc != 2) {
        std::cout << argv[0] << " [instruction stream]" << std::endl;
        exit(1);
    }

    InstructionStreamReader reader(argv[1]);
    Event event;

    bool cooling = false;
    std::uint64_t event_count = 0, instruction_count = 0, memop_count = 0, memory_count = 0, sysreg_count = 0,
                  context_count = 0, control_count = 0, cooling_instructions = 0, warmup_instructions = 0, sve = 0;

    while (reader.read(event)) {
        event_count++;
        if (event.has_instruction()) {
            instruction_count++;
            adis::encoding_enum_t inum = adis::decode(event.instruction().opcode());
            if (adis::sve(inum))
                sve++;
            if (cooling)
                cooling_instructions++;
            memop_count += event.instruction().memop_size();
        } else if (event.has_memory()) {
            memory_count++;
        } else if (event.has_sysreg()) {
            sysreg_count++;
        } else if (event.has_context()) {
            context_count++;
        } else if (event.has_control()) {
            control_count++;
            if (event.control().type() == Control::START_MEASUREMENT) {
                warmup_instructions = instruction_count;
            } else if (event.control().type() == Control::STOP_MEASUREMENT) {
                cooling = true;
            }
        }
        event.Clear();
    }

    const char nl = '\n';
    std::cout << std::dec << "stream version = " << reader.version() << nl
              << "total instruction count = " << instruction_count << nl << "  (" << warmup_instructions << " warmup, "
              << instruction_count - warmup_instructions - cooling_instructions << " measurement, and "
              << cooling_instructions << " cooldown)" << nl << "ARM SVE instruction count = " << sve << nl
              << "memop count = " << memop_count << nl << "memory count = " << memory_count << nl
              << "sysreg count = " << sysreg_count << nl << "context count = " << context_count << nl
              << "control count = " << control_count << nl << "total event count = " << event_count << nl;
}

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
