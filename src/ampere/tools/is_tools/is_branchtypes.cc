/*
 * Copyright (c) 2019-2025 Ampere Computing. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <iostream>
#include <string>
#include <ampere/archx/branch_type.h>
#include <getopt.h>

#include "ampere/instruction_stream/instruction_stream.h"

using namespace ampere::is;

enum branch_category {
    UNCONDITIONAL,
    CONDITIONAL,
    NO_PSTATE,
    INDIRECT,
    CALL_RET,
};

struct branch_desc
{
    const char* instr_name; // name of instruction used for printing purposes
    uint32_t opcode;        // Op code of branch instruction
    uint32_t bitmask;       // 0 = care, 1 = don't care
    branch_category conditional;
    unsigned long count; // Number of instructions found
};

struct branch_desc branches[] = {{"CBZ", 0x34000000, 0x80ffffff, NO_PSTATE, 0},
                                 {"CBNZ", 0x35000000, 0x80ffffff, NO_PSTATE, 0},
                                 {"TBZ", 0x36000000, 0x80ffffff, NO_PSTATE, 0},
                                 {"TBNZ", 0x37000000, 0x80ffffff, NO_PSTATE, 0},
                                 {"B", 0x14000000, 0x03ffffff, UNCONDITIONAL, 0},
                                 {"BL", 0x94000000, 0x03ffffff, UNCONDITIONAL, 0},
                                 {"BR", 0xd61f0000, 0x000003e0, INDIRECT, 0},
                                 {"BLR", 0xd63f0000, 0x000003e0, INDIRECT, 0},
                                 {"RET", 0xd65f0000, 0x000003e0, CALL_RET, 0},
                                 {"B.EQ", 0x54000000, 0x00ffffe0, CONDITIONAL, 0},
                                 {"B.NE", 0x54000001, 0x00ffffe0, CONDITIONAL, 0},
                                 {"B.CS", 0x54000002, 0x00ffffe0, CONDITIONAL, 0},
                                 {"B.CC", 0x54000003, 0x00ffffe0, CONDITIONAL, 0},
                                 {"B.MI", 0x54000004, 0x00ffffe0, CONDITIONAL, 0},
                                 {"B.PL", 0x54000005, 0x00ffffe0, CONDITIONAL, 0},
                                 {"B.VS", 0x54000006, 0x00ffffe0, CONDITIONAL, 0},
                                 {"B.VC", 0x54000007, 0x00ffffe0, CONDITIONAL, 0},
                                 {"B.HI", 0x54000008, 0x00ffffe0, CONDITIONAL, 0},
                                 {"B.LS", 0x54000009, 0x00ffffe0, CONDITIONAL, 0},
                                 {"B.GE", 0x5400000a, 0x00ffffe0, CONDITIONAL, 0},
                                 {"B.LT", 0x5400000b, 0x00ffffe0, CONDITIONAL, 0},
                                 {"B.GT", 0x5400000c, 0x00ffffe0, CONDITIONAL, 0},
                                 {"B.LE", 0x5400000d, 0x00ffffe0, CONDITIONAL, 0},
                                 {"B.AL", 0x5400000e, 0x00ffffe1, UNCONDITIONAL, 0},
                                 {nullptr, 0, 0, CALL_RET, 0}};

void usage(int argc, char** argv)
{
    printf("Usage: %s INSTRUCTION_STREAM [option] <trace>\n", argv[0]);
    printf("  --csv   output csv format\n");
    printf("  --header output leading header\n");
    printf("  --help  display this help and exit\n");
}

int main(int argc, char** argv)
{
    bool opt_csv = false;
    bool opt_header = false;

    const char* const short_opts = "cdh";
    const option long_opts[] = {{"csv", no_argument, nullptr, 'c'},
                                {"header", no_argument, nullptr, 'd'},
                                {"help", no_argument, nullptr, 'h'},
                                {nullptr, no_argument, nullptr, 0}};

    // Parse Arguments
    while (true) {
        const auto opt = getopt_long(argc, argv, short_opts, long_opts, nullptr);

        if (opt == -1)
            break;

        switch (opt) {
        case 'c':
            opt_csv = true;
            break;
        case 'd':
            opt_header = true;
            break;

        case '?': // Unknown option
            printf("Unknown argument '%c'\n", opt);
            usage(argc, argv);
            return -1;
        case 'h':
        default:
            usage(argc, argv);
            return 0;
        }
    }

    // Validate arguments
    // Header only makes sense with csv option
    if (opt_header && !opt_csv) {
        printf("Header flag valid only with csv flag\n");
        usage(argc, argv);
        return -1;
    }

    // Main program
    InstructionStreamReader* reader = new InstructionStreamReader(argv[optind]);
    Instruction instruction;
    unsigned long total_instr = 0;
    unsigned long unconditional = 0;
    unsigned long conditional = 0;
    unsigned long no_pstate = 0;
    unsigned long indirect = 0;
    unsigned long ret = 0;

    while (reader->read(instruction)) {
        uint32_t opcode = instruction.opcode();
        total_instr++;

        struct branch_desc* bp = branches;
        while (bp->instr_name != nullptr) {
            if ((opcode & ~bp->bitmask) == bp->opcode) {
                bp->count++;
                switch (bp->conditional) {
                case UNCONDITIONAL:
                    unconditional++;
                    break;
                case CONDITIONAL:
                    conditional++;
                    break;
                case NO_PSTATE:
                    no_pstate++;
                    break;
                case INDIRECT:
                    indirect++;
                    break;
                case CALL_RET:
                    ret++;
                    break;
                };
            }
            bp++;
        }
    }

    // Print Results
    unsigned long total_branches = 0;
    if (!opt_csv) {
        printf("Branch types\n");
        struct branch_desc* bp = branches;
        while (bp->instr_name != nullptr) {
            total_branches += bp->count;
            printf("  %4s: %lu\n", bp->instr_name, bp->count);
            bp++;
        }

        printf("\nBranch Categories:\n");
        printf("  Unconditional: %lu\n", unconditional);
        printf("    Conditional: %lu\n", conditional);
        printf("       Indirect: %lu\n", indirect);
        printf("    Simple Cond: %lu\n", no_pstate);
        printf("           RETs: %lu\n", ret);

        printf("\nTotal Instructions: %lu\n", total_instr);
        printf("    Total Branches: %lu\n", total_branches);
        printf(" Branch Percentage: %f%%\n", ((double)total_branches) / total_instr * 100);
    } else {
        if (opt_header) {
            // Print header
            struct branch_desc* bp = branches;
            while (bp->instr_name != nullptr) {
                printf("%s, ", bp->instr_name);
                bp++;
            }
            printf("total instructions, total branches, unconditional, conditional, indirect, Simple, ret\n");
        }

        struct branch_desc* bp = branches;
        while (bp->instr_name != nullptr) {
            total_branches += bp->count;
            printf("%lu, ", bp->count);
            bp++;
        }
        printf("%lu, ", total_instr);
        printf("%lu, ", total_branches);
        printf("%lu, ", unconditional);
        printf("%lu, ", conditional);
        printf("%lu, ", indirect);
        printf("%lu, ", no_pstate);
        printf("%lu\n", ret);
    }

    return 0;
}
