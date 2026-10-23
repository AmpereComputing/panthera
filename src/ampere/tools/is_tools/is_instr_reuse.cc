//
// Copyright (c) 2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include <fcntl.h>
#include <iostream>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <deque>
#include <boost/optional.hpp>
#include <boost/program_options.hpp>

#include "ampere/instruction_stream/instruction_stream.h"

using namespace ampere::is;
namespace po = boost::program_options;

int main(int argc, char* argv[])
{
    uint64_t end_inst(0);
    unsigned window(32);

    // instruction buffer
    std::deque<std::uint64_t> inst_queue;

    boost::optional<uint64_t> opt_end_inst;
    boost::optional<unsigned> opt_window;

    po::options_description desc("Allowed options");
    desc.add_options()("help,h", "produce help message")(
        "instrs,e",
        po::value<boost::optional<uint64_t>>(&opt_end_inst),
        "analyze 'arg' instructions after the START_MEASUREMENT marker")(
        "window,w", po::value<boost::optional<unsigned>>(&opt_window), "size of window to look for loops");

    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);

    if (1 == argc || vm.count("help")) {
        std::cerr << desc << "\n";
        std::cerr << "Usage: " << std::endl;
        std::cerr << "  " << argv[0] << " [opts] [path to stream file]" << std::endl;
        return 1;
    }

    end_inst = opt_end_inst ? *opt_end_inst : 10000;
    window = opt_window ? *opt_window : 32;

    InstructionStreamReader reader(argv[argc - 1]);
    Instruction instruction;
    Event event;

    std::uint64_t pc;

    uint64_t instruction_count(0), found(0);

    // Ignore "warmup" instructions before we get to the meat of the
    // instruction stream, which starts with the MEASUREMENT marker
    if (reader.version() >= 2) {
        while (reader.read(event)) {
            if (event.has_control() && Control::START_MEASUREMENT == event.control().type()) {
                break;
            }
            event.Clear();
        }
    }

    // spin through the trace and collect data
    while (reader.read(event)) {
        // record Instruction info
        if (event.has_instruction()) {
            instruction = event.instruction();
            pc = instruction.program_counter().va();

            // check if this PC is already in the IQ
            auto it = find(inst_queue.begin(), inst_queue.end(), pc);
            if (inst_queue.end() != it)
                found++;

            // make room in the queue
            if (inst_queue.size() >= window)
                inst_queue.pop_front();

            // stick the new PC into the queue
            inst_queue.push_back(pc);

            instruction_count++;
            instruction.Clear();

            if (instruction_count > end_inst)
                break;
        }

        event.Clear();
    }

    // print out results
    printf("instruction_count = %ld, instrs found in loop window = %ld\n", instruction_count, found);
    printf("ratio found in loop window = %.3f\n", (double)found / (double)instruction_count);
}

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
