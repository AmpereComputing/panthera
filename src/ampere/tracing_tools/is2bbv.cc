//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include <iostream>

#include "ampere/instruction_stream/instruction_stream.h"
#include "bbv.h"
#include "encodings.h"

using namespace ampere::is;

int main(int argc, char **argv)
{
    if (argc != 3) {
        std::cout << "Usage: is2bbv INSTRUCTION_STREAM BBV_FILENAME" << std::endl;
        return -1;
    }

    InstructionStreamReader reader = InstructionStreamReader(argv[1]);
    VariableBBV bbv = VariableBBV(argv[2]);
    Event event;

    bool started = (reader.version() == 1);
    uint64_t bb_instructions = 0;
    while (reader.read(event)) {
        if (event.has_control()) {
            if (event.control().type() == Control_Type_START_MEASUREMENT) {
                started = true;
            } else if (event.control().type() == Control_Type_STOP_MEASUREMENT) {
                break;
            }
        } else if (started && event.has_instruction()) {
            const Instruction &instruction = event.instruction();
            bb_instructions++;
            if (is_branch(instruction.opcode())) {
                bbv.record_bb(instruction.program_counter().va(), bb_instructions);
                bb_instructions = 0;
            }
        }
    }

    bbv.end_bbv();
}

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
