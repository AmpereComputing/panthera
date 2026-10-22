//
// Copyright (c) 2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
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

struct Stats
{
    uint64_t instructions{0};
    uint64_t store_barriers{0};
};

void usage(const std::string& prog)
{
    std::string program_name = fs::path{prog}.filename().native();
    std::cerr << "usage: " << program_name << " [-x <col>=<val>...] <instruction stream>" << std::endl;
    exit(-1);
}

Stats count_barriers(const fs::path& stream)
{
    auto reader = new is::InstructionStreamReader(stream);
    is::Instruction instruction;

    Stats stats;

    while (reader->read(instruction)) {
        ++stats.instructions;

        auto uops = archx::furiosa::decode(instruction.opcode());

        for (auto& u : uops) {
            auto barrier_type = u->info->barrier_type;

            if (barrier_type == archx::BarrierType::DMB_ST || barrier_type == archx::BarrierType::DMB_ALL
                || barrier_type == archx::BarrierType::DSB)
            {
                ++stats.store_barriers;
                break;
            }
        }
    }

    return stats;
}

void print_counts(const std::string& trace, const std::map<std::string, std::string>& meta, const Stats stats)
{
    std::cout << "trace,instructions,store_barriers";

    for (auto& m : meta)
        std::cout << ',' << m.first;

    std::cout << '\n' << trace << ',' << stats.instructions << ',' << stats.store_barriers;

    for (auto& m : meta)
        std::cout << ',' << m.second;

    std::cout << '\n';
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

    print_counts(trace_name(instruction_stream), meta, count_barriers(instruction_stream));

    return 0;
}

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
