//
// Copyright (c) 2018-2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <iostream>
#include <sstream>
#include <string>

#include "ampere/instruction_stream/instruction_stream.h"
#include "ampere/archx/translation.h"
#include "ampere/archx/sysreg.h"
#include "ampere/archx/addr.h"

using namespace std::literals;
using namespace ampere::is;
using namespace ampere::archx;

std::set<uint64_t> mem_pages;
std::map<uint64_t, uint64_t> mem_image;

struct Options
{
    uint64_t wait_instructions{0};
    uint64_t warmup_instructions{5000};
    uint64_t measured_instructions{250000};
    uint64_t cooldown_instructions{5000};
    bool trim_memory{false};
};

template<typename T>
void get_option(T& opt_val, const std::string& argument)
{
    std::istringstream iss{argument};
    iss >> opt_val;
}

void print_help_and_exit()
{
    std::cerr << "Usage: is_chop [options] <input file> <output file>\n";
    std::cerr << "Options:\n";
    std::cerr << "  --wait         Number of instructions to wait (skip) before starting\n";
    std::cerr << "  --warmup       Number of instructions to warm up for\n";
    std::cerr << "  --measure      Number of instructions to measure\n";
    std::cerr << "  --cooldown     Number of instructions to cool down before stopping\n";
    std::cerr << "  --trim-memory  Only keep memory records needed by the reduced trace (does not support PRFM)\n\n";

    exit(1);
}

std::vector<std::string> parse_arguments(std::vector<std::string> arguments, Options& options)
{
    std::vector<std::string> positional_arguments;

    try {
        for (size_t i = 0; i < arguments.size(); i++) {
            if ("--wait"s == arguments[i])
                get_option(options.wait_instructions, arguments.at(++i));
            else if ("--warmup"s == arguments[i])
                get_option(options.warmup_instructions, arguments.at(++i));
            else if ("--measure"s == arguments[i])
                get_option(options.measured_instructions, arguments.at(++i));
            else if ("--cooldown"s == arguments[i])
                get_option(options.cooldown_instructions, arguments.at(++i));
            else if ("--trim-memory"s == arguments[i])
                options.trim_memory = true;
            else if ("--help"s == arguments[i] || "-h"s == arguments[i])
                print_help_and_exit();
            else if (arguments[i][0] == '-')
                throw std::runtime_error{"unrecognized option: "s + arguments[i]};
            else
                positional_arguments.push_back(arguments[i]);
        }
    } catch (std::out_of_range&) {
        std::cerr << "missing argument value" << std::endl;
        print_help_and_exit();
    } catch (std::runtime_error& err) {
        std::cerr << err.what() << std::endl;
        print_help_and_exit();
    }

    return positional_arguments;
}

void record_walk(uint64_t va, Bitfield& tcr, Bitfield& ttbr0, Bitfield& ttbr1, bool is_instr)
{
    Translation tr(VirtAddr(va), tcr, ttbr0, ttbr1, is_instr);

    // Walk the page tables to translate this address, and remember every page touched by the walk
    while (!tr.done()) {
        PhysAddr pte_addr = tr.next_pte();
        uint64_t pte_contents = mem_image[pte_addr.value()];
        tr.pte_fetched(pte_contents);

        mem_pages.insert(pte_addr >> 12);
    }

    // Remember the page of the final translated address
    auto result = tr.result_address().value();
    mem_pages.insert(result >> 12);
}

int main(int argc, char** argv)
{
    Options options;

    auto files = parse_arguments(std::vector<std::string>(argv + 1, argv + argc), options);

    if (files.size() != 2)
        print_help_and_exit();

    boost::filesystem::path input_file_path(files[0]);
    boost::filesystem::path output_file_path(files[1]);

    InstructionStreamReader* reader = new InstructionStreamReader(input_file_path);
    InstructionStreamWriter writer(output_file_path, reader->features());
    Event event;
    Bitfield ttbr0{0}, ttbr1{0}, tcr{0};
    uint64_t instruction_count{0};
    auto total_instructions = options.wait_instructions + options.warmup_instructions + options.measured_instructions
                              + options.cooldown_instructions;

    if (reader->version() < 2) {
        std::cerr << "Error: Instruction stream version < 2" << std::endl;
        exit(1);
    }

    std::cout << "Chopping " << input_file_path << " to " << options.measured_instructions << " instructions with "
              << options.warmup_instructions << " instructions of warmup." << std::endl;
    if (options.trim_memory)
        std::cout << "Trimming initial memory image to only addresses needed by output instructions\n";
    else
        std::cout << "Keeping all initial memory records\n";

    if (options.trim_memory) {
        // First pass -- record all 4k pages touched by the instructions which will be output
        while (reader->read(event) && instruction_count < total_instructions) {
            // Keep track of the sysregs we need to do translations
            if (event.has_sysreg()) {
                const ampere::is::SysReg& is_sysreg = event.sysreg();
                ampere::archx::SysReg archx_sysreg(
                    is_sysreg.op0(), is_sysreg.op1(), is_sysreg.crn(), is_sysreg.crm(), is_sysreg.op2());
                if (archx_sysreg == TTBR0_EL1)
                    ttbr0 = is_sysreg.value();
                else if (archx_sysreg == TTBR1_EL1)
                    ttbr1 = is_sysreg.value();
                else if (archx_sysreg == TCR_EL1)
                    tcr = is_sysreg.value();
            }

            // Keep track of all the memory image we've seen so we can look up PTE values
            if (event.has_memory())
                mem_image[event.memory().address().pa()] = *((uint64_t*)event.memory().value().data());

            if (event.has_instruction() && ++instruction_count > options.wait_instructions) {
                const Instruction& instruction = event.instruction();

                // Record all pages involved in the PC of this instruction and its translation
                record_walk(instruction.program_counter().va(), tcr, ttbr0, ttbr1, true);

                // Record all pages involved in any memory touched by this isntruction and its translation
                for (int i = 0; i < instruction.memop_size(); i++)
                    record_walk(instruction.memop(i).va(), tcr, ttbr0, ttbr1, false);

                // Special case for DV *ZVA instructions, which are not recorded as memory instructions
                // in the stream. The VA is in the single source register.
                auto opcode = instruction.opcode();
                if ((opcode & 0xffffff00) == 0xd50b7400)
                    record_walk(instruction.register_(0).value(), tcr, ttbr0, ttbr1, false);

                // FIXME: we do not handle software prefetches here
            }

            event.Clear();
        }

        // Set up for the second pass
        reader = new InstructionStreamReader(input_file_path);
        instruction_count = 0;
    }

    bool started{false}, stopped{false};
    while (reader->read(event) && instruction_count < total_instructions) {
        // Don't write out start/stop_measurement controls, we're changing those
        if (event.has_control()
            && (event.control().type() == Control::START_MEASUREMENT
                || event.control().type() == Control::STOP_MEASUREMENT))
            goto next_event;

        // For memory events before we start outputting instructions, only write them out if
        // they are to a page touched by the output instructions.
        if (options.trim_memory && event.has_memory() && instruction_count <= options.wait_instructions) {
            auto first_byte = event.memory().address().pa();
            auto last_byte = first_byte + event.memory().value().size() - 1;
            auto first_page = first_byte >> 12;
            auto last_page = last_byte >> 12;
            if (!mem_pages.contains(first_page) && (first_page == last_page || !mem_pages.contains(last_page)))
                goto next_event;
        }

        if (event.has_instruction()) {
            instruction_count++;

            if (instruction_count <= options.wait_instructions)
                goto next_event;

            if (!started && instruction_count > options.wait_instructions + options.warmup_instructions) {
                Event control_event;
                Control control;
                control.set_type(Control::START_MEASUREMENT);
                control_event.mutable_control()->CopyFrom(control);
                writer.write(control_event);
                started = true;
            }

            if (!stopped
                && instruction_count
                       > options.wait_instructions + options.warmup_instructions + options.measured_instructions)
            {
                Event control_event;
                Control control;
                control.set_type(Control::STOP_MEASUREMENT);
                control_event.mutable_control()->CopyFrom(control);
                writer.write(control_event);
                stopped = true;
            }
        }

        writer.write(event);

    next_event:
        event.Clear();
    }

    writer.finalize();

    if (instruction_count < options.wait_instructions + options.warmup_instructions + options.measured_instructions
                                + options.cooldown_instructions)
    {
        std::cerr << "Error: Did not successfully read enough instructions from the input stream" << std::endl;
        exit(1);
    }

    return 0;
}

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
