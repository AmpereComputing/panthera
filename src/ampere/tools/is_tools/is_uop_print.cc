//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include <deque>
#include <vector>
#include <string>
#include <boost/filesystem/path.hpp>
#include <fmt/format.h>
#include <fmt/ostream.h>
#include <fmt/ranges.h>
namespace fs = boost::filesystem;
#include "ampere/archx/instruction_is.h"
#include "ampere/archx/uopcodes.h"
#include "ampere/archx/uop.h"
#include "ampere/instruction_stream/instruction_stream.h"
#include "chip-decoder-map.h"

using namespace ampere;

static std::uint64_t fsu_count = 0;

static void print_uopset(const archx::UopSet& uops, bool spacing = true)
{
    bool first = true;
    for (auto& uop : uops) {
        if (first) {
            fmt::print("{:16s} ", "");
            first = false;
        } else if (spacing) {
            fmt::print("{:16s} ", "");
        }

        if (uop->is_fsu()) {
            fmt::print("FSU  ");
            ++fsu_count;
        } else if (uop->is_load_fsu()) {
            fmt::print("FSUL ");
            ++fsu_count;
        } else if (uop->is_single_uop_store()) {
            fmt::print("SUS  ");
        } else {
            fmt::print("     ");
        }
        fmt::print("{}\n", *uop);
    }
}

static void print_properties(uint32_t itext)
{
    uint64_t pc = 0;
    archx::InstructionIS inst(archx::ProgramCounter{pc}, itext);
    const bool ld = inst.is_load();
    const bool st = inst.is_store();
    const bool pf = inst.is_prefetch();
    if (ld || st || pf) {
        fmt::print("Properties: ");
        if (ld) {
            fmt::print("\tLoad");
        }
        if (st) {
            fmt::print("\tStore");
        }
        if (pf) {
            fmt::print("\tPrefetch");
        }
        fmt::print("\n");
    }
}

static void decode_print(archx::ProgramCounter pc, uint32_t itext, bool spacing = true)
{
    auto instr = archx::InstructionIS(pc, itext);
    fmt::print("aarch64 {:016x} {}\n", pc.value(), instr.disasm());
    auto uops = archx::Instruction::decode(itext);
    if (uops.size()) {
        print_uopset(uops, spacing);
        print_properties(itext);
    }
}

void print_uops(const fs::path& stream)
{
    auto reader = new is::InstructionStreamReader(stream);
    is::Instruction instruction;

    while (reader->read(instruction)) {
        archx::ProgramCounter pc(instruction.program_counter().va());
        decode_print(pc, instruction.opcode());
    }
}

std::string trace_name(fs::path p)
{
    if (p.has_extension()) {
        while (p.extension() != ".is")
            p = p.stem();

        if (p.has_extension())
            p = p.stem();
    }

    return p.native();
}
static uint32_t get_int(std::deque<std::string>& cmdline)
{
    if (cmdline.empty()) {
        throw std::runtime_error("Need an integer argument for option\n");
    }
    uint32_t r = std::stoul(cmdline.front(), nullptr, 16);
    cmdline.pop_front();
    return r;
}

static std::string get_string(std::deque<std::string>& cmdline)
{
    if (cmdline.empty()) {
        throw std::runtime_error("Need an string argument for option\n");
    }
    std::string s = cmdline.front();
    cmdline.pop_front();
    return s;
}

static void usage(char const* prog)
{
    fmt::print("Usage {} [options] -i trace-stream\n", prog);
    char const* umsg[] = {"-i PATH       file path to trace stream (required)",
                          "-r itext      The (hex) value for one instructions",
                          "-d decoder    Name of decoder to use, repeatable",
                          "-c chip       Name of chip to use (selects decoders). Default: furiosa",
                          nullptr};

    for (int i = 0; umsg[i]; i++)
        fmt::print("\t{}\n", umsg[i]);
    exit(EXIT_FAILURE);
}

int main(int argc, char** argv)
{
    std::deque<std::string> cmdline(argv + 1, argv + argc);
    std::vector<std::string> decoder_names;
    fs::path instruction_stream;
    bool got_itext = false;
    bool got_chip = false;
    bool got_istream = false;
    uint32_t itext = 0;

    while (!cmdline.empty()) {
        auto arg = cmdline.front();
        cmdline.pop_front();

        if (arg == "-r") {
            if (got_itext) {
                throw std::runtime_error("Only one itext (-r) allowed\n");
            }
            itext = get_int(cmdline);
            got_itext = true;
        } else if (arg == "-d") {
            decoder_names.emplace_back(get_string(cmdline));
        } else if (arg == "-c") {
            if (got_chip) {
                throw std::runtime_error("Only one chip (-c) arg allowed\n");
            }
            got_chip = true;
            auto chip = get_string(cmdline);
            decoder_names = is_tools::get_decoders(chip);
        } else if (arg == "--help" || arg == "-h") {
            usage(argv[0]);
        } else if (arg == "-i") {
            if (got_istream) {
                throw std::runtime_error("Only one istream (-i) arg allowed\n");
            }
            got_istream = true;
            instruction_stream = get_string(cmdline);
        } else {
            usage(argv[0]);
        }
    }

    if (!decoder_names.size()) {
        decoder_names.emplace_back("furiosa");
    }
    fmt::print("Setting decoders to: [{}]\n", fmt::join(decoder_names, ", "));
    archx::Instruction::set_decoders(decoder_names);

    if (got_itext && !instruction_stream.empty()) {
        usage(argv[0]);
    }

    if (!instruction_stream.empty()) {
        print_uops(instruction_stream);
        fmt::print("fsu_count: {}\n", fsu_count);
    } else if (got_itext) {
        fmt::print("itext {:08x}\n", itext);
        decode_print(archx::ProgramCounter(0), itext, false);
    } else {
        usage(argv[0]);
    }
}

