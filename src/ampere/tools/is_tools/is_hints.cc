//
// Copyright (c) 2019-2024 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <deque>
#include <fmt/core.h>

#include "ampere/instruction_stream/instruction_stream.h"
#include "ampere/adis/src/adis.h"

using namespace ampere::is;
static void usage(char const* prog)
{
    constexpr const char* usage = R"("Usage {} [options] instruction-stream.is.xz
    -w, --include-warmup   output warmup instructions in addition to "measurement"
    -h, --help             this help message
)";
    fmt::print(usage, prog);
    exit(EXIT_FAILURE);
}

int main(int argc, char** argv)
{
    bool include_warmup_instrs = false;
    std::optional<std::string> instruction_stream_fn = std::nullopt;
    std::deque<std::string> cmdline(argv + 1, argv + argc);
    while (!cmdline.empty()) {
        auto arg = cmdline.front();
        cmdline.pop_front();
        if (arg == "-h" || arg == "--help") {
            usage(argv[0]);
        } else if (arg == "-w" || arg == "--include-warmup") {
            include_warmup_instrs = true;
        } else {
            if (instruction_stream_fn.has_value()) {
                fmt::print(stderr, "Can only specify one instruction stream file\n");
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
    Event event;

    long instruction_count = 0;
    bool measurement_started = (reader.version() < 2) || include_warmup_instrs;
    if (!measurement_started)
        fmt::print("Ignoring instructions before START_MEASUREMENT event\n");
    while (reader.read(event)) {
        if (event.has_instruction()) {
            instruction_count++;
            Instruction instruction = event.instruction();
            if (measurement_started) {
                const adis::encoding_enum_t r = adis::decode(instruction.opcode());
                if (r == adis::ENC_HINT_HM_hints) {
                    fmt::print("Instruction: inum: {} pc: 0x{:016x} op: 0x{:08x} dis: `{}`\n",
                               instruction_count,
                               instruction.program_counter().va(),
                               instruction.opcode(),
                               adis::disasm(instruction.opcode(), r));
                }
            }
        } else if (event.has_control()) {
            Control control = event.control();
            fmt::print("Control: type: {}\n", Control::Type_Name(control.type()));
            if (control.type() == Control::START_MEASUREMENT) {
                fmt::print("Omitted {} instructions before measurement began.\n", instruction_count);
                measurement_started = true;
            }
        }
        event.Clear();
    } // while
}

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
