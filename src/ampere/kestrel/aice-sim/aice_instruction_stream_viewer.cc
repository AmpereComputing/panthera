//
// Copyright (c) 2019-2024 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include <cstdint>
#include <deque>
#include <string>
#include <optional>
#include <iostream>
#include <format>
#include "aice_instruction_stream.h"
#include "ampere/kestrel/aice-sim/dtype.hh"
using namespace ampere::kestrel;

static void usage(char const* prog)
{
    std::cout << std::format(R"("Usage {} [options] aice-instruction-stream.is.xz

    -h, --help             this help message
)",
                             prog);
    exit(EXIT_FAILURE);
}

int main(int argc, char** argv)
{
    std::optional<std::string> instruction_stream_fn = std::nullopt;
    std::deque<std::string> cmdline(argv + 1, argv + argc);
    while (!cmdline.empty()) {
        auto arg = cmdline.front();
        cmdline.pop_front();
        if (arg == "-h" || arg == "--help") {
            usage(argv[0]);
        } else {
            if (instruction_stream_fn.has_value()) {
                std::cerr << std::format("Can only specify one instruction stream file\n");
                exit(EXIT_FAILURE);
            }
            instruction_stream_fn = arg;
        }
    }
    if (!instruction_stream_fn.has_value()) {
        std::cerr << std::format("Need an instruction stream file name\n");
        exit(EXIT_FAILURE);
    }

    aice::is::InstructionStreamReader reader(instruction_stream_fn.value());
    aice::is::Event event;

    std::cout << std::format("Instruction stream is version {} with the following feature support:\n",
                             reader.version());
    std::cout << std::format("  {:35s}: {}\n", "format_version", reader.features().format_version());
    std::cout << std::format("\n");

    long event_count = 0, instruction_count = 0, control_count = 0;
    bool measurement_started = true;
    if (!measurement_started) {
        std::cout << std::format("Ignoring instructions before START_MEASUREMENT event\n");
    }
    while (reader.read(event)) {
        event_count++;
        if (event.has_instruction()) {
            instruction_count++;
            const aice::is::Instruction& instruction = event.instruction();
            if (measurement_started) {
                std::cout << std::format("Instruction: inum: {} ", instruction_count);
                if (instruction.transpose()) {
                    std::cout << std::format("transpose ");
                }
                if (auto is_dtype = instruction.dtype(); is_dtype != aice::is::dtype::no_d_type) {
                    const auto dtype = static_cast<aice_emu::dtype_t>(is_dtype);
                    std::cout << std::format("dtype: {} ", dtype_t2str(dtype));
                }
                std::cout << std::format("pc: 0x{:x} ", instruction.program_counter().pa());
                std::cout << std::format("op: {} ", aice::is::Opcode_Name(instruction.opcode()));

                for (int i = 0; i < instruction.gpr_src_size(); ++i) {
                    std::cout << std::format(
                        "gsrc{}={:#x} ", instruction.gpr_src(i).number(), instruction.gpr_src(i).value());
                }
                for (int i = 0; i < instruction.gpr_dst_size(); ++i) {
                    std::cout << std::format(
                        "gdst{}={:#x} ", instruction.gpr_dst(i).number(), instruction.gpr_dst(i).value());
                }
                for (int i = 0; i < instruction.imm_src_size(); ++i) {
                    std::cout << std::format("imm={:#x} ", instruction.imm_src(i));
                }

                for (int i = 0; i < instruction.amr_src_size(); ++i) {
                    std::cout << std::format(
                        "gsrc{}=amr{} ", instruction.amr_src(i).number(), instruction.amr_src(i).value());
                }
                for (int i = 0; i < instruction.amr_dst_size(); ++i) {
                    std::cout << std::format(
                        "gdst{}=amr{} ", instruction.amr_dst(i).number(), instruction.amr_dst(i).value());
                }

                for (int i = 0; i < instruction.imr_src_size(); ++i) {
                    std::cout << std::format(
                        "gsrc{}=imr{} ", instruction.imr_src(i).number(), instruction.imr_src(i).value());
                }
                for (int i = 0; i < instruction.imr_dst_size(); ++i) {
                    std::cout << std::format(
                        "gdst{}=imr{} ", instruction.imr_dst(i).number(), instruction.imr_dst(i).value());
                }

                for (int i = 0; i < instruction.breg_src_size(); ++i) {
                    std::cout << std::format(
                        "gsrc{}=breg{} ", instruction.breg_src(i).number(), instruction.breg_src(i).value());
                }
                for (int i = 0; i < instruction.breg_dst_size(); ++i) {
                    std::cout << std::format(
                        "gdst{}=breg{} ", instruction.breg_dst(i).number(), instruction.breg_dst(i).value());
                }

                for (int i = 0; i < instruction.scale_src_size(); ++i) {
                    std::cout << std::format(
                        "gsrc{}=scale{} ", instruction.scale_src(i).number(), instruction.scale_src(i).value());
                }
                for (int i = 0; i < instruction.scale_dst_size(); ++i) {
                    std::cout << std::format(
                        "gdst{}=scale{} ", instruction.scale_dst(i).number(), instruction.scale_dst(i).value());
                }

                for (int i = 0; i < instruction.mem_src_size(); ++i) {
                    std::cout << std::format(
                        "gsrc{}=mem{} ", instruction.mem_src(i).number(), instruction.mem_src(i).pa());
                }
                for (int i = 0; i < instruction.mem_dst_size(); ++i) {
                    std::cout << std::format(
                        "gdst{}=mem{} ", instruction.mem_dst(i).number(), instruction.mem_dst(i).pa());
                }

                std::cout << std::format("\n");
            }
        } else if (event.has_control()) {
            const aice::is::Control& control = event.control();
            std::cout << std::format("Control: type: {}\n", aice::is::Control::Type_Name(control.type()));
            control_count++;
            if (control.type() == aice::is::Control::START_MEASUREMENT) {
                // std::cout << std::format("Omitted {} instructions before measurement began\n", instruction_count);
                measurement_started = true;
            }
        }
        event.Clear();
    }

    std::cout << std::format("instruction count = {}\n", instruction_count);
    std::cout << std::format("control count = {}\n", control_count);
    std::cout << std::format("total event count = {}\n", event_count);
}
