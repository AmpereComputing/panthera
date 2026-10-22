//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

// Check that the committed instructions in two event streams contain the same
// PCs (and inum's as a sanity-check). This is intended to be used to ensure
// that event streams being used in performance validation match each other
// (because if we're not executing the same code, comparing the performance
// doesn't make sense).

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <iostream>
#include <unordered_map>
#include <optional>

#include <boost/program_options.hpp>
#include <fmt/format.h>

#include "ampere/archx/addr.h"
#include "ampere/event_stream/event_stream_reader.h"

namespace po = boost::program_options;

struct stream_context
{
    std::string prefix;
    unsigned skip_instructions;
    std::unordered_map<int, ampere::event_stream_proto::Definition> definitions;
    int def_instruction_commit, def_inum, def_commit_pc;
};

struct stream_context panthera_context, rtl_context;

struct committed_instruction
{
    unsigned inum{0};
    ampere::archx::ProgramCounter pc{0x0};
};

std::optional<struct committed_instruction> next_committed_instruction(ampere::event_stream::EventStreamReader& stream,
                                                                       struct stream_context& context)
{
    ampere::event_stream_proto::Record record;
    while (true) {
        auto r = stream.read();
        if (!r)
            break;

        auto& record = *r;
        if (!record.has_event() && !record.has_definition())
            continue;

        // Record definitions and their ID mappings as we encounter them (at
        // least for the events we are tracking)
        if (record.has_definition()) {
            auto definition = record.definition();
            auto def_id = definition.id();
            context.definitions[def_id] = definition;

            if (0 == std::string(context.prefix + ".gpc.instruction_commit").compare(definition.name())) {
                context.def_instruction_commit = def_id;
            } else if (0 == std::string(context.prefix + ".gpc.inum").compare(definition.name())) {
                context.def_inum = def_id;
            } else if (0 == std::string(context.prefix + ".gpc.commit_pc").compare(definition.name())) {
                context.def_commit_pc = def_id;
            }
        } else if (record.has_event()) {
            auto event = record.event();

            if (event.definition_id() == context.def_instruction_commit) {
                if (context.skip_instructions > 0) {
                    context.skip_instructions--;
                    record.Clear();
                    continue;
                }

                struct committed_instruction inst;
                for (int i = 0; i < event.values_size(); i++) {
                    auto value = event.values(i);
                    if (value.definition_id() == context.def_inum) {
                        if (value.has_int_value())
                            inst.inum = value.int_value();
                        else
                            inst.inum = value.uint_value();
                    } else if (value.definition_id() == context.def_commit_pc) {
                        inst.pc = ampere::archx::ProgramCounter(value.uint_value());
                    }
                }
                return inst;
            }
        }
        record.Clear();
    }

    return std::nullopt;
}

int main(int argc, char* argv[])
{
    unsigned check_instructions(0);

    po::options_description desc("Allowed options");
    desc.add_options()("help,h", "print help message")("check-instructions,i",
                                                       po::value<unsigned>(&check_instructions)->default_value(30000),
                                                       "number of instructions to validate")(
        "rtl-prefix,p",
        po::value<std::string>(&rtl_context.prefix)->default_value("sys.soc_0.core_0"),
        "top level prefix to use for RTL events")(
        "panthera-prefix,P",
        po::value<std::string>(&panthera_context.prefix)->default_value("sys.soc_0.core_0"),
        "top level prefix to use for panthera events")(
        "rtl-skip-instructions,s",
        po::value<unsigned>(&rtl_context.skip_instructions)->default_value(0),
        "Number of instructions to skip at beginning of RTL event stream")(
        "panthera-skip_instructions,S",
        po::value<unsigned>(&panthera_context.skip_instructions)->default_value(0),
        "Number of instructions to skip at beginning of panthera event stream");

    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);

    if (argc < 3 || vm.count("help")) {
        std::cerr << desc << "\n";
        std::cerr << "Usage: " << std::endl;
        std::cerr << "  " << argv[0] << " [opts] <RTL event stream> <panthera event stream>" << std::endl;
        exit(vm.count("help") ? EXIT_SUCCESS : EXIT_FAILURE);
    }

    ampere::event_stream::EventStreamReader rtl_es(argv[argc - 2]);
    ampere::event_stream::EventStreamReader panthera_es(argv[argc - 1]);

    // Loop through committed instructions in both streams until we find a
    // mismatch or have seen `check-instructions`
    unsigned instruction_count;
    for (instruction_count = 1; instruction_count <= check_instructions; instruction_count++) {
        auto rtl_instruction = next_committed_instruction(rtl_es, rtl_context);
        auto panthera_instruction = next_committed_instruction(panthera_es, panthera_context);

        if (!rtl_instruction) {
            std::cerr << fmt::format("RTL event stream ended after checking {} instructions\n", instruction_count - 1);
            exit(EXIT_FAILURE);
        } else if (!panthera_instruction) {
            std::cerr << fmt::format("Panthera event stream ended after checking {} instructions\n",
                                     instruction_count - 1);
            exit(EXIT_FAILURE);
        }

        if (rtl_instruction->inum != panthera_instruction->inum) {
            std::cerr << fmt::format("inum mismatch for checked instruction #{}. RTL inum {}, panthera inum {}.\n",
                                     instruction_count,
                                     rtl_instruction->inum,
                                     panthera_instruction->inum);
            exit(EXIT_FAILURE);
        }
        if (rtl_instruction->pc != panthera_instruction->pc) {
            std::cerr << fmt::format(
                "Commit PC mismatch for {}th instruction (inum {}). RTL PC {:016x}, panthera PC {:016x}.\n",
                instruction_count,
                rtl_instruction->inum,
                rtl_instruction->pc.value(),
                panthera_instruction->pc.value());
            exit(EXIT_FAILURE);
        }
    }

    std::cout << fmt::format("Successfully validated {} committed instructions had the same inums and PCs.\n",
                             check_instructions);
    exit(EXIT_SUCCESS);
}

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
