//
// Copyright (c) 2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include <capstone.h>
#include <platform.h>

#include <boost/algorithm/string/split.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/program_options.hpp>
#include <iostream>
#include <map>

#include "ampere/archx/instruction_is.h"
#include "ampere/archx/furiosa.h"
#include "ampere/archx/uop.h"
#include "ampere/instruction_stream/instruction_stream.h"
#include <fmt/format.h>

namespace fs = boost::filesystem;
namespace po = boost::program_options;

using namespace ampere;

static csh disasm_handle; // capstone

#define MAX_UOPS_PER_INSTR 100
#define MAX_INSTR_ID ARM64_INS_ENDING
struct Stats
{
    uint64_t instructions{0};
    uint64_t uops_per_instruction[MAX_UOPS_PER_INSTR] = {0};
    uint64_t uops_by_instr[MAX_INSTR_ID][MAX_UOPS_PER_INSTR] = {{0}};
    std::string names[MAX_INSTR_ID];
};

void usage(const std::string& prog)
{
    std::string program_name = fs::path{prog}.filename().native();
    std::cerr << "usage: " << program_name << " [-x <col>=<val>...] <instruction stream>" << std::endl;
    exit(EXIT_FAILURE);
}

struct inst_info_t
{
    unsigned int id;
    std::string mnemonic;
};

struct inst_info_t get_opcode_info(std::uint32_t bytes, std::uint64_t pc)
{
    size_t count(0);
    cs_insn* insn;
    struct inst_info_t rc;

    // place the instruction bytes into the format required by capstone
    uint8_t byte_array[4];
    byte_array[0] = bytes;
    byte_array[1] = bytes >> 8;
    byte_array[2] = bytes >> 16;
    byte_array[3] = bytes >> 24;

    count = cs_disasm(disasm_handle, byte_array, 4, pc, 0, &insn);
    if (count) {
        rc.id = insn[0].id;
        rc.mnemonic = insn[0].mnemonic;
    } else {
        rc.id = 0;
        rc.mnemonic = "invalid opcode";
    }
    cs_free(insn, count);
    return rc;
}

Stats count_uops(const fs::path& stream)
{
    auto reader = new is::InstructionStreamReader(stream);
    is::Instruction instruction;

    Stats stats;
    std::cerr << "Working:";
    uint32_t i = 0;
    while (reader->read(instruction)) {
        i++;
        const uint32_t mil = 1000 * 1000;
        if ((i % mil) == 0) {
            std::cerr << i / mil << "M " << std::flush;
        }
        ++stats.instructions;

        auto uops = archx::furiosa::decode(instruction.opcode());
        uint32_t n = uops.size();
        if (n > MAX_UOPS_PER_INSTR - 1)
            n = MAX_UOPS_PER_INSTR - 1;
        stats.uops_per_instruction[n]++;

        inst_info_t iinfo = get_opcode_info(instruction.opcode(), instruction.program_counter().va());
        assert(iinfo.id < MAX_INSTR_ID);
        stats.uops_by_instr[iinfo.id][n]++;
        if (stats.names[iinfo.id].size() == 0)
            stats.names[iinfo.id] = iinfo.mnemonic;
    }
    std::cerr << "done" << std::endl;
    return stats;
}

void print_counts_global(const std::string& trace, const std::map<std::string, std::string>& meta, const Stats stats)
{
    std::cout << "GI,trace,instructions";
    for (int i = 0; i < MAX_UOPS_PER_INSTR; i++) {
        std::cout << ",uops" << i;
    }

    for (auto& m : meta)
        std::cout << ',' << m.first;
    std::cout << '\n';

    std::cout << "GI," << trace << ',' << stats.instructions;
    for (int i = 0; i < MAX_UOPS_PER_INSTR; i++) {
        std::cout << ',' << stats.uops_per_instruction[i];
    }

    for (auto& m : meta)
        std::cout << ',' << m.second;
    std::cout << '\n';
}

void print_counts_per_instr(const std::string& trace, const std::map<std::string, std::string>& meta, const Stats stats)
{
    std::cout << "PI,instruction";
    for (int i = 0; i < MAX_UOPS_PER_INSTR; i++) {
        std::cout << ",uops" << i;
    }
    std::cout << '\n';
    for (int j = 0; j < MAX_INSTR_ID; j++) {
        if (stats.names[j].size() > 0) {
            std::cout << "PI," << stats.names[j];
            for (int i = 0; i < MAX_UOPS_PER_INSTR; i++) {
                std::cout << ',' << stats.uops_by_instr[j][i];
            }
            std::cout << '\n';
        }
    }
}

void print_counts(const std::string& trace, const std::map<std::string, std::string>& meta, const Stats stats)
{
    print_counts_global(trace, meta, stats);
    print_counts_per_instr(trace, meta, stats);
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

int main(int argc, char** argv)
{
    fs::path instruction_stream;
    std::map<std::string, std::string> meta;

    po::options_description options("Options");
    options.add_options()("help,h", "print help")(
        "extra,x", po::value<std::vector<std::string>>(), "add <key>=<value> into the output")(
        "instruction_stream", po::value<fs::path>(&instruction_stream)->required(), "instruction stream name");

    po::positional_options_description positional;
    positional.add("instruction_stream", 1);

    po::variables_map vm;

    try {
        po::store(po::command_line_parser(argc, argv).options(options).positional(positional).run(), vm);

        if (vm.count("help"))
            usage(argv[0]);

        po::notify(vm);
    } catch (po::error& err) {
        usage(argv[0]);
    }

    if (vm.count("extra")) {
        for (auto& arg : vm["extra"].as<const std::vector<std::string>&>()) {
            std::vector<std::string> meta_setting;
            boost::split(meta_setting, arg, [](char c) { return c == '='; });
            meta[meta_setting[0]] = meta_setting[1];
        }
    }

    // set up capstone for disassembly
    cs_err err = cs_open(CS_ARCH_ARM64, CS_MODE_ARM, &disasm_handle);
    if (err) {
        std::cout << "Failed on cs_open() with error returned: " << err << std::endl;
        assert(0);
    }
    print_counts(trace_name(instruction_stream), meta, count_uops(instruction_stream));
}

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
