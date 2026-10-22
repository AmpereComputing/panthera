//
// Copyright (c) 2019-2025 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include <fcntl.h>
#include <ios>
#include <iostream>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <cstdint>
#include <deque>
#include <string>
#include <fmt/core.h>
#include <fstream>
#include "ampere/instruction_stream/instruction_stream.h"
#include "disasm.h"
#include "export_instruction.h"
#include <zlib.h>

using namespace ampere::is;
static void usage(char const* prog)
{
    constexpr const char* usage = R"("Usage {} [options] instruction-stream.is.xz [output_file]

    -w, --exclude-warmup   do not output warmup instructions; only emit "measurement"
    -h, --help             this help message
)";

    fmt::print(usage, prog);
    exit(EXIT_FAILURE);
}

const InstClass arminstr_mType(uint32_t opcode)
{
    // std::cout << std::uppercase << std::hex << opcode << std::endl;
    adis::encoding_enum_t r = adis::decode(opcode);

    if (adis::load(r))
        return InstClass::loadInstClass;
    else if (adis::store(r))
        return InstClass::storeInstClass;

    else if (adis::branch(r)) {
        if (adis::branch_cond(r))
            return InstClass::condBranchInstClass;

        else if (adis::branch_call(r) && adis::branch_indirect(r))
            return InstClass::uncondIndirectBranchInstClass;

        else if (adis::branch_call(r))
            return InstClass::callDirectInstClass;

        else if (adis::branch_return(r))
            return InstClass::ReturnInstClass;

        else if (adis::branch_indirect(r))
            return InstClass::callIndirectInstClass;

        else
            return InstClass::uncondDirectBranchInstClass;
    }
    // This is a best guess
    else if (divide(r) || fp_divide(r) || int_divide(r) || divide_reversed(r) || sqrt(r) || rsqrt(r))
        return InstClass::slowAluInstClass;
    /*else if()
       return InstClass::fpInstClass;
    else if (w)
       return InstClass::aluInstClass;
    */
    else
        return InstClass::aluInstClass;
    // return InstClass::undefInstClass;
}

inline bool is_br(InstClass inst_class)
{
    const bool cond_br = (inst_class == InstClass::condBranchInstClass);
    const bool uncond_dir_br = (inst_class == InstClass::uncondDirectBranchInstClass);
    const bool uncond_ind_br = (inst_class == InstClass::uncondIndirectBranchInstClass);
    const bool call_dir_br = (inst_class == InstClass::callDirectInstClass);
    const bool call_ind_br = (inst_class == InstClass::callIndirectInstClass);
    const bool ret_br = (inst_class == InstClass::ReturnInstClass);

    return (cond_br | uncond_dir_br | uncond_ind_br | call_dir_br | call_ind_br | ret_br);
}

inline bool is_direct(InstClass inst_class)
{
    const bool cond_br = (inst_class == InstClass::condBranchInstClass);
    const bool uncond_dir_br = (inst_class == InstClass::uncondDirectBranchInstClass);
    const bool call_dir_br = (inst_class == InstClass::callDirectInstClass);

    return (cond_br | uncond_dir_br | call_dir_br);
}

inline bool is_conditional(InstClass inst_class)
{
    return (inst_class == InstClass::condBranchInstClass);
}

uint64_t branch_target(uint32_t opcode, uint64_t pc)
{
    adis::encoding_enum_t r = adis::decode(opcode);
    return adis::branch_target(r, opcode, pc);
}

void gzwrite_or_fail(struct gzFile_s* outFile, const char* buffer, size_t len)
{
    size_t written = gzwrite(outFile, buffer, len);
    if (written != len)
        throw std::runtime_error("Failed to write all bytes to output file");
}

void write_Binary_Instruction(const exportedInstr& instr, struct gzFile_s* outFile)
{
    gzwrite_or_fail(outFile, reinterpret_cast<const char*>(&instr.mPc), sizeof(instr.mPc));
    gzwrite_or_fail(outFile, reinterpret_cast<const char*>(&instr.mType), sizeof(instr.mType));

    gzwrite_or_fail(outFile, reinterpret_cast<const char*>(&instr.mTaken), sizeof(instr.mTaken));
    if (instr.mTaken)
        gzwrite_or_fail(outFile, reinterpret_cast<const char*>(&instr.mNextPc), sizeof(instr.mNextPc));

    gzwrite_or_fail(outFile, reinterpret_cast<const char*>(&instr.mNumInRegs), sizeof(instr.mNumInRegs));
    gzwrite_or_fail(outFile, reinterpret_cast<const char*>(&instr.mNumOutRegs), sizeof(instr.mNumOutRegs));
}

void write_NOP_Binary_Instruction(const exportedInstr& instr, struct gzFile_s* outFile)
{
    gzwrite_or_fail(outFile, reinterpret_cast<const char*>(&instr.mPc), sizeof(instr.mPc));
    gzwrite_or_fail(outFile, reinterpret_cast<const char*>(&instr.mType), sizeof(instr.mType));

    gzwrite_or_fail(outFile, reinterpret_cast<const char*>(&instr.mNumInRegs), sizeof(instr.mNumInRegs));
    gzwrite_or_fail(outFile, reinterpret_cast<const char*>(&instr.mInRegs[0]), sizeof(instr.mInRegs[0]));

    gzwrite_or_fail(outFile, reinterpret_cast<const char*>(&instr.mNumOutRegs), sizeof(instr.mNumOutRegs));
    gzwrite_or_fail(outFile, reinterpret_cast<const char*>(&instr.mOutRegs[0]), sizeof(instr.mOutRegs[0]));
    gzwrite_or_fail(outFile, reinterpret_cast<const char*>(&instr.mOutRegsValues[0]), sizeof(instr.mOutRegsValues[0]));
}

// Write a magic number as the first 8 bytes of this trace such that we can
// check for it and differentiate these traces from previous CBP traces.
void write_magic_number(struct gzFile_s* outFile)
{
    const char magic[9] = "CBPNGAmp";
    for (unsigned i = 0; i < sizeof(magic) - 1; i++) {
        gzwrite_or_fail(outFile, &magic[i], 1);
    }
}

int main(int argc, char** argv)
{
    bool include_warmup_instrs = true;

    std::optional<std::string> instruction_stream_fn = std::nullopt;
    std::optional<std::string> output_trace_fn = std::nullopt;
    std::deque<std::string> cmdline(argv + 1, argv + argc);
    while (!cmdline.empty()) {
        auto arg = cmdline.front();
        cmdline.pop_front();
        if (arg == "-h" || arg == "--help") {
            usage(argv[0]);
        } else if (arg == "-w" || arg == "--exclude-warmup") {
            include_warmup_instrs = false;
        } else {
            if (!instruction_stream_fn.has_value()) {
                instruction_stream_fn = arg;
            } else if (!output_trace_fn.has_value()) {
                output_trace_fn = arg;
            } else {
                fmt::print(stderr, "Unexpected argument: already found input and output file names\n");
                exit(EXIT_FAILURE);
            }
        }
    }
    if (!instruction_stream_fn.has_value()) {
        fmt::print(stderr, "Need an instruction stream file name\n");
        exit(EXIT_FAILURE);
    }
    if (!output_trace_fn.has_value()) {
        output_trace_fn = instruction_stream_fn.value() + ".gz";
    }

    InstructionStreamReader reader(instruction_stream_fn.value());
    Event event;

    long event_count = 0, instruction_count = 0, branch_count = 0, conditional_direct = 0, taken_branches = 0;
    bool measurement_started = (reader.version() < 2) || include_warmup_instrs;
    if (!measurement_started)
        std::cout << "Ignoring instructions before START_MEASUREMENT event" << std::endl;

    // This is the arm format file
    auto* outFile = gzopen(output_trace_fn.value().c_str(), "wb");
    if (!outFile) {
        std::cerr << "Failed to open file for writing: " << output_trace_fn.value() << std::endl;
        exit(EXIT_FAILURE);
    }

    write_magic_number(outFile);

    bool has_event = reader.read(event);
    event_count++;
    while (has_event) {
        if (event.has_instruction() && (include_warmup_instrs || measurement_started)) {
            instruction_count++;
            const Instruction& instruction = event.instruction();

            exportedInstr instr = exportedInstr();
            instr.mPc = instruction.program_counter().va();
            instr.mType = arminstr_mType(instruction.opcode());
            // since they are not load nor store no effectiveAddr/memsize/baseUpdate/reg offset... will be filled or
            // written
            instr.mNumInRegs = 0;
            instr.mNumOutRegs = 0;

            // This implementation targets branch prediction study only, so we
            // record only branch instructions.
            if (is_br(instr.mType)) {
                // for all kind of branch we will check the next instr PC to see
                // if this branch was taken or not
                branch_count++;

                Event peek_event;
                bool has_peek_event = reader.read(peek_event);
                event_count++;
                while (has_peek_event) {
                    if (peek_event.has_instruction()) {
                        instruction_count++;
                        const Instruction& one_ahead_instruction = peek_event.instruction();
                        if (is_conditional(instr.mType)) {
                            // Conditional branches must be direct, and are
                            // only considered taken if the next PC is the same
                            // as their target
                            instr.mTaken = (one_ahead_instruction.program_counter().va() != (instr.mPc + 4));
                            conditional_direct++;
                        } else {
                            // Unconditional branches must always be taken
                            instr.mTaken = true;
                        }
                        if (instr.mTaken) {
                            if (is_direct(instr.mType)) {
                                instr.mNextPc = branch_target(instruction.opcode(), instr.mPc);
                            } else { /* indirects */
                                instr.mNextPc = one_ahead_instruction.program_counter().va();
                            }
                            taken_branches++;
                        }

                        // break the peek event/instr while loop
                        break;
                    }
                    has_peek_event = reader.read(peek_event);
                    event_count++;
                }
                has_event = has_peek_event;
                event = peek_event;

                // If we are at the end of the trace, we may fail to set
                // instr.mTaken to true above for unconditional branches. In
                // this case, force taken to true and set mNextPc
                if (!is_conditional(instr.mType) && !instr.mTaken) {
                    instr.mTaken = true;
                    if (is_direct(instr.mType)) {
                        instr.mNextPc = branch_target(instruction.opcode(), instr.mPc);
                    } else { /* indirects */
                        instr.mNextPc = 0x0;
                    }
                    taken_branches++;
                }

                write_Binary_Instruction(instr, outFile);
            } else { // not branch
                // we will write "MOV R0, R0" NOP instruction for now
                instr.mType = InstClass::aluInstClass;

                instr.mNumInRegs = 1;
                instr.mInRegs.resize(1);
                instr.mInRegs[0] = 0;

                instr.mNumOutRegs = 1;
                instr.mOutRegs.resize(1);
                instr.mOutRegs[0] = 0;
                instr.mOutRegsValues.resize(1);
                instr.mOutRegsValues[0] = 0x0;

                write_NOP_Binary_Instruction(instr, outFile);
                has_event = reader.read(event);
                event_count++;
            }
        } else // measurement did not start or event doesnt have instruction
        {
            /*if (event.has_control()) {
                const Control& control = event.control();
                control_count++;
                if (control.type() == Control::START_MEASUREMENT) {
                    if (!include_warmup_instrs)
                        std::cout << "Omitted " << std::dec << instruction_count << " instructions before measurement
            began." << std::endl; measurement_started = true;
                }
            }*/
            event_count++;
            has_event = reader.read(event);
        }
    } // while there are events

    gzflush(outFile, Z_FINISH);
    gzclose(outFile);

    std::cout << std::dec;
    std::cout << "total event count = " << event_count << std::endl;
    std::cout << "instruction count = " << instruction_count << std::endl;
    std::cout << "branch count = " << branch_count << std::endl;
    std::cout << "conditional branches = " << conditional_direct << std::endl;
    std::cout << "taken branches = " << taken_branches << std::endl;
}

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
