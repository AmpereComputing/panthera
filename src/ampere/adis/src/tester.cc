//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <iomanip>
#include <string>
#include <bitset>
#include <fmt/core.h>
#include "adis.h"

static std::uint32_t  binary_to_u32(std::string s) {
    std::bitset<32> b32(s,2);  // skip first 2 characters ("0b")
    return b32.to_ulong();
}

struct stats_t {
    std::uint64_t tests{0};
    std::uint64_t pass{0};
    std::uint64_t errors{0};
};

static int test_one(stats_t& stats, std::string s) {
    std::uint32_t itext = 0;
    stats.tests++;
    adis::encoding_enum_t r = adis::ENC_INVALID;
    std::uint64_t va = 0;
    if (s.substr(0,2) == "0b") {
        itext = binary_to_u32(s);
    }
    else {
        itext = std::stoul(s,0,16);
    }

    r = adis::decode(itext);
    if (r == adis::ENC_INVALID) {
        std::cout << "Error\n";
        stats.errors++;
        return 1;
    }
    stats.pass++;
    std::cout << std::setfill('0') << std::hex
              << std::setw(8) << itext
              << std::setfill(' ') << std::dec
              << " " << r << ": "
              << adis::asm_prototype(r)
              << "  /// " << adis::encoding_name(r)
              << "  /// " << adis::disasm(itext, r, va) << '\n';
    return 0;

}


static int test_branch_direct(stats_t& stats, std::uint32_t itext, std::uint64_t pc, std::uint64_t tgt) {
    stats.tests++;
    adis::encoding_enum_t r = adis::ENC_INVALID;
    r = adis::decode(itext);
    if (r == adis::ENC_INVALID) {
        std::cout << "Error\n";
        stats.errors++;
        return 1;
    }
    std::uint64_t ctgt = 0;
    if (adis::branch(r) && !adis::branch_indirect(r)) {
        ctgt = adis::branch_target(r, itext, pc);
        if (ctgt == tgt) {
            stats.pass++;
            return 0;
        }
    }

    fmt::print("itext={:08x} {}={} proto: {} disas: {}\n",
               itext,
               static_cast<std::uint32_t>(r), adis::encoding_name(r),
               adis::asm_prototype(r),
               adis::disasm(itext, r, pc));

    fmt::print("Program counter {:016x}\n", pc);
    fmt::print("Computed target {:016x}\n", ctgt);
    fmt::print("Expected target {:016x}\n", tgt);
    stats.errors++;
    return 1;
}

int main(int argc, char** argv) {
    stats_t stats;

    test_one(stats, "5ac02001");
    // B
    test_branch_direct(stats, 0x17000000L, 0, 0xfffffffffc000000ULL);
    test_branch_direct(stats, 0x14000001L, 8, 0xC);
    // B.cond
    test_branch_direct(stats, 0b01010100'000'0000'0000'0000'0001'00000, 4, 8);
    test_branch_direct(stats, 0b01010100'100'0000'0000'0000'0001'00000, 4, 0xfffffffffff00008);
    //                                   ^                     ^
    // TBNZ
    test_branch_direct(stats, 0b0'01'101'1'1'00000'00'0000'0000'0001'00000, 4, 8);
    test_branch_direct(stats, 0b0'01'101'1'1'00000'10'0000'0000'0001'00000, 4, 0xffffffffffff8008);
    //                                             ^               ^
    // CBNZ 32
    test_branch_direct(stats, 0b0'011010'1'0000'0000'0000'0000'001'11110, 4, 8);
    // CBZ 32
    test_branch_direct(stats, 0b0'011010'0'0000'0000'0000'0000'001'11110, 4, 8);

    if (stats.errors) {
        std::cout << "FAILS: " << stats.errors  << "\n";
    }
    std::cout << "PASS: " << stats.pass << "/" << stats.tests << " " << 100.0*stats.pass/stats.tests << "\n";

    return (stats.errors != 0);
    (void) argc; (void) argv;
}
