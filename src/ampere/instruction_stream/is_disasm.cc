//
// Copyright (c) 2019-2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <fmt/core.h>
#include <unistd.h>
#include <deque>
#include <optional>
#include <list>
#include <string>

#include "ampere/instruction_stream/instruction_stream.h"
#include "disasm.h"

using namespace ampere::is;

struct IStash
{
    int count{0};        // frequency
    std::string dis;     // disasm
    std::uint32_t bytes; // raw bytes
    unsigned el;         // privilege level: {0,1,2,3}
    std::list<std::string> properties;
};

using InstMap = std::map<std::uint64_t, IStash>; // instructions at each unique pc

static std::uint64_t get_u64(std::deque<std::string>& cmdline)
{
    if (cmdline.empty()) {
        fmt::print(stderr, "Need an integer argument for option\n");
        exit(EXIT_FAILURE);
    }
    std::uint64_t r = std::stoull(cmdline.front());
    cmdline.pop_front();
    return r;
}

static void usage(char const* prog)
{
    constexpr const char* usage = R"("Usage {} [options] instructiion-stream.is.xz

    -e NUM                  number of instructions to disassemble. Default: unlimited
    -a, --adis              use ADIS for disassembly
    -w, --include-warmup    output warmup instructions in addition to "measurement"
    -c, --include-cooldown  output cooldown instructions in addition to "measurement"
    -p, --properties        emit instruction properties
    -h, --help              this help message
)";

    fmt::print(usage, prog);
    exit(EXIT_FAILURE);
}

int main(int argc, char* argv[])
{
    std::optional<std::uint64_t> end_inst = std::nullopt;

    std::uint64_t instrs_per_el[4]{0};
    unsigned curr_el = 0;
    bool include_warmup_instrs = false;
    bool include_cooldown_instrs = false;
    bool use_adis = false;
    std::optional<std::string> instruction_stream_fn = std::nullopt;
    std::deque<std::string> cmdline(argv + 1, argv + argc);
    bool properties = false;
    while (!cmdline.empty()) {
        auto arg = cmdline.front();
        cmdline.pop_front();
        if (arg == "-e" || arg == "--e") { // allow double-dash for backwards compatibilty
            end_inst = get_u64(cmdline);
        } else if (arg == "-h" || arg == "--help") {
            usage(argv[0]);
        } else if (arg == "-a" || arg == "--adis") {
#if defined(USING_CAPSTONE) && defined(USING_ADIS)
            // only act on the arg if also using capstone
            use_adis = true;
#endif
        } else if (arg == "-w" || arg == "--include-warmup") {
            include_warmup_instrs = true;
        } else if (arg == "-c" || arg == "--include-cooldown") {
            include_cooldown_instrs = true;
        } else if (arg == "-p" || arg == "--properties") {
            properties = true;
        } else {
            if (instruction_stream_fn.has_value()) {
                fmt::print(stderr, "Can only specify one instruction stream file name\n");
                exit(EXIT_FAILURE);
            }
            instruction_stream_fn = arg;
        }
    }
    if (!instruction_stream_fn.has_value()) {
        fmt::print(stderr, "Need an instruction stream file name\n");
        exit(EXIT_FAILURE);
    }

    InstructionStreamReader reader(instruction_stream_fn.value());
    Instruction instruction;
    Event event;

    ampere::disasm::init(use_adis);

    InstMap instrs;

    std::uint64_t pc;
    std::uint64_t instruction_count = 0;

    // Conditionally ignore "warmup" instructions before we get to the
    // meat of the instruction stream, which starts with the
    // MEASUREMENT marker.
    if (reader.version() >= 2 && include_warmup_instrs == false) {
        while (reader.read(event)) {
            // record EL changes
            if (event.has_context()) {
                Context context = event.context();

                if (Context::EL == context.type()) {
                    curr_el = context.value();
                }
            }

            if (event.has_control() && Control::START_MEASUREMENT == event.control().type()) {
                break;
            }
            event.Clear();
        }
    }

    // spin through the trace and collect data
    while (reader.read(event)) {
        // record EL changes
        if (event.has_context()) {
            Context context = event.context();

            if (Context::EL == context.type()) {
                curr_el = context.value();
            }
        }

        // record Instruction info
        else if (event.has_instruction())
        {
            instruction = event.instruction();
            pc = instruction.program_counter().va();
            IStash* ii = &instrs[pc];
            if (0 == ii->count) {
                ii->dis = ampere::disasm::disasm(instruction.opcode(), pc);
                ii->bytes = instruction.opcode();
                ii->el = curr_el;
                if (properties) {
                    adis::encoding_enum_t r = adis::decode(instruction.opcode());
                    if (adis::sve_family(r))
                        ii->properties.emplace_back("SVE*");
                    ii->properties.emplace_back(adis::category(r));
                }
            }
            ii->count++;

            instruction_count++;
            instrs_per_el[curr_el]++;
            instruction.Clear();

            if (end_inst.has_value() && instruction_count > end_inst.value())
                break;
        }

        if (!include_cooldown_instrs && event.has_control() && event.control().type() == Control::STOP_MEASUREMENT)
            break;

        event.Clear();
    }

    // print out the table map
    std::uint64_t nextpc(0);
    for (auto i : instrs) {
        std::uint64_t mypc = i.first;
        IStash ii = i.second;

        if (!nextpc)
            nextpc = mypc;

        if (mypc != nextpc) {
            fmt::print("...\n");
        }

        // total instruction count
        int total = ii.count;
        fmt::print("0x{:016x}:{:8}: EL{}", mypc, total, ii.el);
        fmt::print(": {:08x}  {:40s}", ii.bytes, ii.dis.c_str());
        fmt::print(" : ");
        for (const auto& prop : ii.properties) {
            fmt::print(" {}", prop);
        }
        fmt::print("\n");
        nextpc = mypc + 0x4;
    }

    fmt::print("instruction_count = {}\n", instruction_count);
    fmt::print("instrs_per_el[0, 1, 2, 3] = {{{}, {}, {}, {}}}, ratios = {{{:.3f}, {:.3f}, {:.3f}, {:.3f}}}\n",
               instrs_per_el[0],
               instrs_per_el[1],
               instrs_per_el[2],
               instrs_per_el[3],
               instrs_per_el[0] / (double)instruction_count,
               instrs_per_el[1] / (double)instruction_count,
               instrs_per_el[2] / (double)instruction_count,
               instrs_per_el[3] / (double)instruction_count);
}

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
