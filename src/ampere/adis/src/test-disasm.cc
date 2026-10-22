//
// Copyright (c) 2026 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <cstdint>
#include <iostream>
#include <string_view>

#include "adis.h"

namespace {

struct TestCase
{
    std::uint32_t instruction;
    std::string_view encoding_name;
    std::string_view disassembly;
};

bool disassembles_as(const TestCase& test)
{
    const auto encoding = adis::decode(test.instruction);
    const auto actual_name = adis::encoding_name(encoding);
    const auto actual_disassembly = adis::disasm(test.instruction, encoding);

    if (actual_name == test.encoding_name && actual_disassembly == test.disassembly) {
        return true;
    }

    std::cerr << "0x" << std::hex << test.instruction << std::dec << " decoded as " << actual_name << " / "
              << actual_disassembly << ", expected " << test.encoding_name << " / " << test.disassembly << '\n';
    return false;
}

} // namespace

int main()
{
    // These cases cover the compact AARCHMRS grammar choices whose raw field
    // ranges must be lowered to ADIS tables or typed operand helpers.
    constexpr TestCase cases[] = {
        {0x8b2043ff, "ADD_64_addsub_ext", "add  sp, sp, w0, uxtw #0"},
        {0x8b2063ff, "ADD_64_addsub_ext", "add  sp, sp, x0, uxtx #0"},
        {0x4e010c00, "DUP_asimdins_DR_r", "dup  v0.16b, w0"},
        {0x4e080c00, "DUP_asimdins_DR_r", "dup  v0.2d, x0"},
        {0x4e011c00, "INS_asimdins_IR_r", "ins  v0.b[0], w0"},
        {0x4e181c00, "INS_asimdins_IR_r", "ins  v0.d[1], x0"},
        {0x38224820, "STRB_32B_ldst_regoff", "strb  w0, [x1, w2, uxtw]"},
    };

    bool passed = true;
    for (const auto& test : cases) {
        passed = disassembles_as(test) && passed;
    }
    return passed ? 0 : 1;
}
