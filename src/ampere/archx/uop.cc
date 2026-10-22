/*
 * Copyright (c) 2022-2024 Ampere Computing. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <iostream>
#include <iomanip>
#include <sstream>
#include "ampere/archx/register.h"
#include "ampere/archx/uop.h"

namespace ampere::archx {

static bool print_reg(std::ostream& o, LReg x, bool emitted = false)
{
    if (x != LReg::INVALID) {
        if (emitted)
            o << ", ";
        o << x;
        return true;
    }
    return emitted;
}

std::ostream& operator<<(std::ostream& o, const struct Uop& uop)
{
    bool emitted = false; // to print comma separators
    o << uop.opcode << ' ';
    emitted = print_reg(o, uop.dst);
    emitted = print_reg(o, uop.dst2, emitted);
    if (uop.produce_flags)
        emitted = print_reg(o, LReg::FLAGS, emitted);
    if (uop.produce_tmpflags)
        (void)print_reg(o, LReg::FLGT0, emitted);
    o << " <- ";
    emitted = print_reg(o, uop.pred);
    emitted = print_reg(o, uop.src1, emitted);
    emitted = print_reg(o, uop.src2, emitted);
    (void)print_reg(o, uop.src3, emitted);
    return o;
}

std::string Uop::disasm() const
{
    std::ostringstream os;
    os << *this;
    return os.str();
}

} // namespace ampere::archx
