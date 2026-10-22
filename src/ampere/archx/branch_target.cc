//
// Copyright (c) 2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/archx/branch_target.h"
#include "ampere/sc/sc.h"

namespace ampere::archx {

static unsigned sign_extend(int x, int bit)
{
    unsigned m = 1U << (bit - 1);
    return (x ^ m) - m;
}

ProgramCounter branch_target_if_taken(ProgramCounter pc, std::uint32_t opcode)
{
    int offset = 0;

    if ((opcode & 0xfe000000) == 0x54000000) // conditional
        offset = static_cast<int>(sign_extend(util::extract(opcode, 23, 5), 19) << 2);
    else if ((opcode & 0x7c000000) == 0x14000000) // unconditional
        offset = static_cast<int>(sign_extend(util::extract(opcode, 25, 0), 26) << 2);
    else if ((opcode & 0x7e000000) == 0x34000000) // compare and branch
        offset = static_cast<int>(sign_extend(util::extract(opcode, 23, 5), 19) << 2);
    else if ((opcode & 0x7e000000) == 0x36000000) // test and branch
        offset = static_cast<int>(sign_extend(util::extract(opcode, 18, 5), 14) << 2);
    else
        AMPERE_FATAL("instruction (0x{:032x}) is not an immediate branch", opcode);

    if (offset < 0)
        return pc - (-offset);
    else
        return pc + offset;
}

} // namespace ampere::archx
