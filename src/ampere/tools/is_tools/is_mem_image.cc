//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include <boost/filesystem.hpp>
#include <boost/program_options.hpp>
#include <fmt/format.h>
#include <fstream>
#include <iostream>

#include "ampere/instruction_stream/instruction_stream.h"

using namespace ampere::is;
namespace po = boost::program_options;
namespace fs = boost::filesystem;

struct PageConfig
{
    static const unsigned int page_size = 4096;
    static const unsigned int page_bits = 12;
    static const unsigned int page_offset_mask = (1u << page_bits) - 1;
};

struct PageData : public std::array<std::uint8_t, PageConfig::page_size>
{
    PageData() : std::array<std::uint8_t, PageConfig::page_size>{0} {}
};

uint64_t overwrites_avoided{0};
std::map<std::uint64_t, PageData> memory;

void usage(std::string progname)
{
    std::cerr << "Usage: " << progname << " <instruction stream>" << std::endl;
    exit(-1);
}

void write_memory(std::uint64_t phys_addr, std::uint64_t data, int size)
{
    std::uint64_t page = phys_addr >> PageConfig::page_bits;
    int offset = phys_addr & PageConfig::page_offset_mask;

    auto& page_data = memory[page];

    for (int i = 0; i < size; i++) {
        uint8_t to_write = data & 0xffu;
        if (page_data[offset] != to_write && page_data[offset] != 0) {
            overwrites_avoided += 1;
            return;
        }
        page_data[offset] = to_write;
        data >>= 8u;
        offset++;
    }
}

void dump_memory(std::string mem_file_name)
{
    std::ofstream mem_file{mem_file_name};

    unsigned int page_size = PageConfig::page_size;

    for (auto& page : memory) {
        std::uint64_t phys_addr = page.first << PageConfig::page_bits;
        mem_file.write((const char*)&phys_addr, sizeof(phys_addr));
        mem_file.write((const char*)&page_size, sizeof(page_size));
        mem_file.write((const char*)page.second.data(), page.second.size());
    }
}

std::string extract_trace_name(fs::path p)
{
    if (p.has_extension()) {
        while (p.extension() != ".is")
            p = p.stem();

        if (p.has_extension())
            p = p.stem();
    }

    return p.native();
}

int main(int argc, char* argv[])
{
    std::string opt_instruction_stream;

    po::options_description desc("Allowed options");
    desc.add_options()("help", "produce help message")("is",
                                                       po::value<std::string>(&opt_instruction_stream)->required(),
                                                       "instruction stream to extract memory image from");

    po::positional_options_description positional;
    positional.add("is", 1);

    po::variables_map vm;

    try {
        po::store(po::command_line_parser(argc, argv).options(desc).positional(positional).run(), vm);

        if (vm.count("help"))
            usage(argv[0]);

        po::notify(vm);
    } catch (po::error& err) {
        usage(argv[0]);
    }

    InstructionStreamReader reader(opt_instruction_stream);
    Instruction instruction;

    while (reader.read(instruction)) {
        std::uint64_t pc_pa = instruction.program_counter().pa();

        // each pc
        write_memory(pc_pa, instruction.opcode(), 4);
    }

    std::string mem_file_name = extract_trace_name(opt_instruction_stream) + ".mem";
    dump_memory(mem_file_name);
    std::cout << "wrote " << mem_file_name << std::endl;
    std::cout << "avoided overwriting previously-written instructions " << overwrites_avoided << " times" << std::endl;

    return 0;
}

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
