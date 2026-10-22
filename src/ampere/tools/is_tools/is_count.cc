//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include <boost/algorithm/string/split.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/program_options.hpp>
#include <iostream>
#include <map>
#include <regex>
#include <memory>

#include "ampere/archx/instruction_is.h"
#include "ampere/archx/furiosa.h"
#include "ampere/instruction_stream/instruction_stream.h"
#include <fmt/format.h>

namespace fs = boost::filesystem;
namespace po = boost::program_options;

using namespace ampere;

void usage(const std::string& prog)
{
    std::string program_name = fs::path{prog}.filename().native();
    std::cerr << "usage: " << program_name << " [-f <regex] [-x <col>=<val>...] <instruction stream>\n\n";
    std::cerr << "Count occurrences of instructions within instruction stream,\n";
    std::cerr << "outputting a histogram of decode sources (i.e. disasm:file:line)." << std::endl;
    exit(-1);
}

std::map<std::string, std::uint64_t> count_instructions(const fs::path& stream, std::optional<std::regex> filter)
{
    auto reader = std::make_unique<is::InstructionStreamReader>(stream);
    is::Instruction instruction;

    std::map<std::string, std::uint64_t> count;

    while (reader->read(instruction)) {
        try {
            const auto* source = archx::furiosa::decode_source(instruction.opcode());
            std::vector<std::string> components;
            boost::split(components, source, [](char c) { return c == ':'; });
            if (!filter || std::regex_search(components[0], *filter)) {
                auto key =
                    fmt::format("{}:{}:{}", components[0], fs::path(components[1]).filename().native(), components[2]);
                ++count[key];
            }
        } catch (std::runtime_error&) {
            ++count["undefined"];
        }
    }

    return count;
}

void print_counts(const std::string& trace,
                  const std::map<std::string, std::string>& meta,
                  const std::map<std::string, std::uint64_t>& counts)
{
    if (meta.empty() && counts.empty())
        return;

    std::cout << "trace,instruction,count";

    for (const auto& m : meta)
        std::cout << ',' << m.first;

    for (const auto& c : counts) {
        std::cout << '\n' << trace << ',' << c.first << ',' << c.second;

        for (const auto& m : meta)
            std::cout << ',' << m.second;
    }

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
    std::optional<std::regex> filter;
    std::map<std::string, std::string> meta;

    po::options_description options("Options");
    options.add_options()("help,h", "print help")(
        "filter,f", po::value<std::string>(), "filter output by regex search within instruction disassembly")(
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
        for (const auto& arg : vm["extra"].as<const std::vector<std::string>&>()) {
            std::vector<std::string> meta_setting;
            boost::split(meta_setting, arg, [](char c) { return c == '='; });
            meta[meta_setting[0]] = meta_setting[1];
        }
    }

    if (vm.count("filter"))
        filter.emplace(vm["filter"].as<std::string>(), std::regex_constants::icase);

    print_counts(trace_name(instruction_stream), meta, count_instructions(instruction_stream, filter));

    return 0;
}

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
