//
// Copyright (c) 2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <deque>
#include <vector>
#include <optional>
#include <cctype>
#include <fmt/core.h>

#include "ampere/archx/addr.h"
#include "ampere/uarch_common/chip_name.h"
#include "cache.h"

using namespace ampere::ms;
using namespace ampere::tracer;
using namespace ampere::archx;
using namespace ampere::uarch_common;

struct ChipData
{
    std::string soc;
    std::uint64_t capacity;
    std::uint64_t associativity;
    ChipName chip;
};

static const uint64_t half_meg = 512 * 1024;
static const uint64_t meg = 1024 * 1024;

static const std::vector<ChipData> chip_data = {{"siryn", 2 * meg, 8, ChipName::siryn},
                                                {"banshee", 2 * meg, 8, ChipName::banshee},
                                                {"rogue", 2 * meg, 8, ChipName::rogue},
                                                {"domino", 2 * meg + half_meg, 10, ChipName::domino}};

static void usage(char const* prog)
{
    fmt::print(stderr, "Usage: {} [options] memory_stream_input_file l2c_warmup_output_file\n", prog);
    char const* umsg[] = {"-c CHIP, --chip CHIP or --soc CHIP",
                          "                     : The SOC (chip) for L2 cache organization. Default: rogue",
                          "                       This determines the capacity, associativity and set/bank hash",
                          "-h,--help            : this help message",
                          nullptr};

    for (int i = 0; umsg[i]; i++) {
        fmt::print(stderr, "\t{}\n", umsg[i]);
    }
    fmt::print(stderr, "Valid SOC (chip) names:\n");
    for (auto [soc, cap, assoc, chip] : chip_data) {
        fmt::print(stderr, "\t{}\n", soc);
    }
    std::exit(EXIT_FAILURE);
}

static std::string get_string(std::deque<std::string>& cmdline)
{
    if (cmdline.empty()) {
        throw std::runtime_error("Need an argument for option");
    }
    std::string s = cmdline.front();
    cmdline.pop_front();
    return s;
}

int main(int argc, char** argv)
{
    std::deque<std::string> cmdline(argv + 1, argv + argc);
    std::vector<std::string> files;
    std::optional<std::uint64_t> associativity;
    std::optional<std::uint64_t> cache_size_bytes;
    ChipName chip_name;
    std::optional<std::string> chip;

    while (!cmdline.empty()) {
        auto arg = cmdline.front();
        cmdline.pop_front();
        if (arg == "--help" || arg == "-h") {
            usage(argv[0]);
        } else if (arg == "-c" || arg == "--chip" || arg == "--soc") {
            chip = get_string(cmdline);
        } else {
            files.emplace_back(arg);
        }
    }

    if (files.size() != 2) {
        throw std::runtime_error(fmt::format("Need two file names"));
    }
    std::string input_fn = files[0];
    std::string output_fn = files[1];

    if (chip.has_value()) {
        bool found = false;
        for (auto [soc, cap, assoc, chip_nm] : chip_data) {
            if (chip.value() == soc) {
                cache_size_bytes = cap;
                associativity = assoc;
                chip_name = chip_nm;
                found = true;
            }
        }
        if (!found) {
            throw std::runtime_error(fmt::format("Invalid chip name: {}", chip.value()));
        }
    } else {
        throw std::runtime_error("A chip name is required");
    }

    MemoryStreamReader reader(input_fn);
    L2Cache<PhysAddr> l2c = L2Cache<PhysAddr>(cache_size_bytes.value(), associativity.value(), chip_name);
    Access access;

    while (reader.read(access)) {
        auto pa = PhysAddr(access.pa());
        if (!l2c.lookup(pa, access.type() == Access_Type_WRITE)) {
            l2c.refill(pa, access.source() == Access_Source_INSTRUCTION, access.type() == Access_Type_WRITE);
        }
        access.Clear();
    }

    l2c.to_memory_stream(output_fn);
}

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
