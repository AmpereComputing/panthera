//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <cstdint>
#include <algorithm>
#include <cstdlib>
#include <string>
#include <bitset>
#include "fmt/format.h"
#include "adis.h"

static void remove_char(std::string& s, char c) {
    s.erase(std::remove(s.begin(), s.end(), c), s.end());
}
static std::uint32_t binary_to_u32(std::string s) {
    // remove periods and single quotes from the bit string
    remove_char(s,'.');
    remove_char(s,'\'');
    std::bitset<32> b32(s,2);  // skip first 2 characters ("0b")
    return b32.to_ulong();
}

static std::string quote(const std::string& s) {
    return fmt::format("\"{}\"", s);
}
static const std::string indent = "    ";
static std::string jstr(const std::string& key, const std::string& value) {
    return fmt::format("{}{}: {}", indent, quote(key), quote(value));
}
static std::string jhex32(const std::string& key, const std::uint32_t value) {
    return fmt::format("{}{}: 0x{:08X}", indent, quote(key), value);
}
static std::string jbool(const std::string& key, const bool value) {
    return fmt::format("{}{}: {}", indent, quote(key), value);
}

int main(int argc, char** argv) {
    std::uint32_t itext = 0;
    std::uint64_t va = 0;
    if (argc < 2) {
        fmt::print(stderr, "Need one argument, default is hex. Use '0b' prefix for binary\n");
        fmt::print(stderr, "Optional 2nd argument virtual address in hex\n");
        fmt::print(stderr, "Binary numbers can have period ('.') or single quote characters as ignored separators.\n");
        exit(EXIT_FAILURE);
    }
    std::string s(argv[1]);
    if (s.substr(0,2) == "0b") {
        itext = binary_to_u32(s);
    }
    else {
        itext = std::stoul(s,0,16);
    }
    if (argc == 3) {
        std::string va_str(argv[2]);
        va = std::stoul(va_str,0,16);
    }

    const adis::encoding_enum_t r = adis::decode(itext);
    if (r == adis::ENC_INVALID) {
        fmt::print("{{\n{},\n{}\n}}\n",
                   jhex32("itext", itext),
                   jstr("encoding_name", adis::encoding_name(r)));
        exit(EXIT_FAILURE);
    }

    std::optional<std::uint64_t> target{std::nullopt};
    if (branch(r) && !branch_indirect(r)) {
        target = branch_target(r, itext, va);
        fmt::print("{:016x} -> {:016x}\n", va, target.value());
    }

    fmt::print("{{\n{},\n{},\n{},\n{},\n{}\n{}\n{}\n}}\n",
               jhex32("itext", itext),
               jstr("prototype", adis::asm_prototype(r)),
               jstr("encoding_name", adis::encoding_name(r)),
               jstr("feature", adis::isa_feature(r)),
               jstr("disasm", adis::disasm(itext, r, va)),
               jbool("pre_index", adis::pre_index(r)),
               jbool("post_index", adis::post_index(r)));

    exit(EXIT_SUCCESS);
}
