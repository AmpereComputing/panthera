//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include <fcntl.h>
#include <iostream>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <math.h>
#include <boost/optional.hpp>
#include <boost/program_options.hpp>
#include <fmt/format.h>

#include "ampere/instruction_stream/instruction_stream.h"

using namespace ampere::is;
namespace po = boost::program_options;

static std::unordered_map<std::uint64_t, std::uint32_t> iside_histo_line_total;
static std::unordered_map<std::uint64_t, std::uint32_t> iside_histo_page_total;
static std::unordered_map<std::uint64_t, std::uint32_t> dside_histo_line_total;
static std::unordered_map<std::uint64_t, std::uint32_t> dside_histo_phys_line_total;
static std::unordered_map<std::uint64_t, std::uint32_t> dside_histo_page_total;

static std::unordered_map<std::uint64_t, std::uint32_t> iside_histo_line_interval;
static std::unordered_map<std::uint64_t, std::uint32_t> iside_histo_page_interval;
static std::unordered_map<std::uint64_t, std::uint32_t> dside_histo_line_interval;
static std::unordered_map<std::uint64_t, std::uint32_t> dside_histo_phys_line_interval;
static std::unordered_map<std::uint64_t, std::uint32_t> dside_histo_page_interval;

static const int line_bits = 6;  // 64 byte lines
static const int page_bits = 12; // 4 kbyte pages

static const bool ISIDE = false;
static const bool DSIDE = true;

static std::uint32_t iside_lookups = 0;
static std::uint32_t dside_lookups = 0;

static void update_addr_pte(std::uint64_t addr, bool dside)
{
    std::uint64_t line = addr >> line_bits;
    if (dside) {
        ++dside_lookups;
        ++dside_histo_line_total[line];
        ++dside_histo_line_interval[line];
    } else {
        ++iside_lookups;
        ++iside_histo_line_total[line];
        ++iside_histo_line_interval[line];
    }
}

static void update_tlb(std::uint64_t vaddr, bool dside)
{
    // FIXME: translate, do the walks
    update_addr_pte(vaddr, dside);
}

static void update_addr_data(std::uint64_t vaddr, std::uint64_t paddr, std::uint32_t size)
{
    std::uint64_t line = vaddr >> line_bits;
    std::uint64_t phys_line = paddr >> line_bits;
    std::uint64_t page = vaddr >> page_bits;

    ++dside_histo_line_total[line];
    ++dside_histo_phys_line_total[phys_line];
    ++dside_histo_page_total[page];
    ++dside_histo_line_interval[line];
    ++dside_histo_page_interval[page];

    update_tlb(vaddr, DSIDE);

    // check for line splits and page splits
    std::uint64_t end_addr = vaddr + size;
    std::uint64_t end_line = end_addr >> line_bits;
    std::uint64_t end_page = end_addr >> page_bits;

    std::uint64_t end_phys_addr = paddr + size;
    std::uint64_t end_phys_line = end_phys_addr >> line_bits;

    if (end_line != line) {
        ++dside_histo_line_total[end_line];
        ++dside_histo_line_interval[end_line];
    }

    if (end_phys_line != phys_line) {
        ++dside_histo_line_total[end_line];
        ++dside_histo_line_interval[end_line];
    }

    if (end_page != page) {
        ++dside_histo_page_total[end_page];
        ++dside_histo_page_interval[end_page];
        update_tlb(end_addr, DSIDE);
    }
}

static void update_addr_code(std::uint64_t addr)
{
    std::uint64_t line = addr >> line_bits;
    std::uint64_t page = addr >> page_bits;

    ++iside_histo_line_total[line];
    ++iside_histo_page_total[page];
    ++iside_histo_line_interval[line];
    ++iside_histo_page_interval[page];

    update_tlb(addr, ISIDE);
}

static void reset_totals(void)
{
    iside_histo_line_total.clear();
    dside_histo_line_total.clear();
    iside_histo_page_total.clear();
    dside_histo_phys_line_total.clear();
    dside_histo_page_total.clear();
}

static void reset_interval(void)
{
    iside_histo_line_interval.clear();
    dside_histo_line_interval.clear();
    iside_histo_page_interval.clear();
    dside_histo_phys_line_interval.clear();
    dside_histo_page_interval.clear();
}

static void print_totals(float line_fsize)
{
    fmt::print(
        " iside footprint in kB: {}\n dside footprint in kB: {}\n dside footprint in kB (Physical): {}\n synonyms: "
        "{}\n iside 4k pages touched: {}\n dside 4k pages touched: {}\n",
        (float)iside_histo_line_total.size() * line_fsize / 1024,
        (float)dside_histo_line_total.size() * line_fsize / 1024,
        (float)dside_histo_phys_line_total.size() * line_fsize / 1024,
        (float)(dside_histo_line_total.size() - dside_histo_phys_line_total.size())
            / (float)(dside_histo_line_total.size()),
        iside_histo_page_total.size(),
        dside_histo_page_total.size());
}

static void print_interval(uint64_t instruction_count, float line_fsize)
{
    fmt::print("{}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}\n",
               instruction_count,
               (float)iside_histo_line_total.size() * line_fsize / 1024,
               (float)dside_histo_line_total.size() * line_fsize / 1024,
               (float)dside_histo_phys_line_total.size() * line_fsize / 1024,
               (float)(dside_histo_line_total.size() - dside_histo_phys_line_total.size())
                   / (float)(dside_histo_line_total.size()),
               iside_histo_page_total.size(),
               dside_histo_page_total.size(),

               (float)iside_histo_line_interval.size() * line_fsize / 1024,
               (float)dside_histo_line_interval.size() * line_fsize / 1024,
               (float)dside_histo_phys_line_interval.size() * line_fsize / 1024,
               (float)(dside_histo_line_interval.size() - dside_histo_phys_line_interval.size())
                   / (float)(dside_histo_line_interval.size()),
               iside_histo_page_interval.size(),
               dside_histo_page_interval.size());
}

int main(int argc, char* argv[])
{
    uint64_t interval(0);

    boost::optional<uint64_t> opt_end_inst, opt_interval;
    bool opt_warmup;

    po::options_description desc("Allowed options");
    desc.add_options()("help", "produce help message")("end,e",
                                                       po::value<boost::optional<uint64_t>>(&opt_end_inst),
                                                       "run footprint for 'arg' instructions from the start")(
        "interval,i",
        po::value<boost::optional<uint64_t>>(&opt_interval),
        "dump footprint info csv file at 'arg' instruction interval")(
        "warmup,w", po::bool_switch(&opt_warmup), "report warmup footprint");

    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);

    if (1 == argc || vm.count("help")) {
        std::cerr << desc << "\n";
        std::cerr << "Usage: " << std::endl;
        std::cerr << "  " << argv[0] << " [opts] [path to stream file]" << std::endl;
        return 1;
    }

    interval = opt_interval ? *opt_interval : 0;

    InstructionStreamReader reader(argv[argc - 1]);
    Event event;

    std::uint64_t pc;

    uint64_t instruction_count = 0;
    int line_size = pow(2.0, line_bits);
    float line_fsize = line_size * 1.0;
    bool in_measured = false;

    if (opt_warmup) {
        fmt::print("Warmup Footprint:\n");
    }

    if (interval) {
        fmt::print(
            "inst_count, iside_kb (total), dside_kb (total), dside_phys_kb (total), synonyms (total), iside_pages_4k "
            "(total), dside_pages_4k (total), iside_kb (interval {}), dside_kb (interval {}), dside_phys_kb (interval "
            "{}), synonyms (interval {}), iside_pages_4k (interval {}), dside_pages_4k (interval {})\n",
            interval,
            interval,
            interval,
            interval,
            interval,
            interval);
    }

    // spin through the trace and collect data
    while (reader.read(event)) {
        Instruction instruction;
        if (reader.version() >= 2) {
            if (event.has_control()) {
                if (event.control().type() == Control::START_MEASUREMENT) {
                    if (opt_warmup) {
                        if (interval) {
                            print_interval(instruction_count, line_fsize);
                        } else {
                            print_totals(line_fsize);
                        }

                        reset_totals();
                        reset_interval();
                        instruction_count = 0;

                        fmt::print("Measured Footprint:\n");
                    }
                    in_measured = true;
                } else if (event.control().type() == Control::STOP_MEASUREMENT) {
                    break;
                }
                continue;
            }
        } else {
            in_measured = true;
        }

        if (event.has_instruction()) {
            instruction = event.instruction();
            if (in_measured || opt_warmup) {
                pc = instruction.program_counter().va();

                // each pc
                update_addr_code(pc);

                // loads and stores
                if (instruction.memop_size() > 0) {
                    for (int i = 0; i < instruction.memop_size(); i++) {
                        update_addr_data(instruction.memop(i).va(), instruction.memop(i).pa(), 1 /*FIXME*/);
                    }
                }

                instruction_count++;

                if (interval && (0 == instruction_count % interval)) {
                    print_interval(instruction_count, line_fsize);

                    reset_interval();
                }

                if (in_measured && opt_end_inst && (instruction_count > *opt_end_inst))
                    break;
            }
        }

        event.Clear();
    }

    if (!interval) {
        print_totals(line_fsize);
    }
}

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
