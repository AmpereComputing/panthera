//
// Copyright (c) 2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include <fcntl.h>
#include <iostream>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <list>
#include <boost/optional.hpp>
#include <boost/program_options.hpp>
#include <fmt/format.h>

#include "ampere/instruction_stream/instruction_stream.h"
#include "ampere/memory_stream/memory_stream.h"

using namespace ampere::is;
using namespace ampere::ms;
namespace po = boost::program_options;

class LRUFilter
{
    using Set = std::list<uint64_t>;
    Set set;
    std::unordered_map<uint64_t, Set::iterator> addresses;
    size_t set_size{0};
    const size_t line_bits{6};

    size_t max_pc_profiles{0};
    std::unordered_map<uint64_t, uint64_t> pc_miss_map;

    void shrink(size_t size)
    {
        while (set.size() > size) {
            addresses.erase(set.back());
            set.pop_back();
        }
    }

    void fill(uint64_t addr)
    {
        uint64_t line_addr = addr >> line_bits;

        if (set_size) {
            shrink(set_size - 1);
        }

        set.push_front(line_addr);
        addresses[line_addr] = set.begin();
    }

public:
    enum class LookupType {
        ISIDE,
        DSIDE,
        NONE
    };

    uint64_t ilookups{0};
    uint64_t dlookups{0};
    uint64_t imisses{0};
    uint64_t dmisses{0};

    void resize(size_t size)
    {
        set_size = size >> line_bits;
        shrink(set_size);
    }

    void reset_stats(void)
    {
        dlookups = 0;
        ilookups = 0;
        dmisses = 0;
        imisses = 0;
    }

    bool lookup(uint64_t pc, uint64_t addr, LookupType access_type)
    {
        uint64_t line_addr = addr >> line_bits;
        if (access_type == LookupType::DSIDE) {
            dlookups++;
        } else if (access_type == LookupType::ISIDE) {
            ilookups++;
        }

        auto hit = addresses.find(line_addr);
        if (hit != addresses.end()) {
            if (hit->second != set.begin()) {
                set.splice(set.begin(), set, hit->second);
            }

            return true;
        } else {
            if (access_type == LookupType::DSIDE) {
                if (max_pc_profiles > 0) {
                    pc_miss_map[pc]++;
                }
                dmisses++;
            } else if (access_type == LookupType::ISIDE) {
                imisses++;
            }
            fill(addr);

            return false;
        }
    }

    size_t size(void) const { return set_size << line_bits; }

    void enable_pc_profiling(size_t max_pcs) { max_pc_profiles = max_pcs; }

    void disable_pc_profiling(void) { max_pc_profiles = 0; }

    void print_pc_profile(void) const
    {
        if (max_pc_profiles > 0) {
            std::vector<std::pair<uint64_t, uint64_t>> sortvec;
            for (const auto& miss : pc_miss_map) {
                sortvec.emplace_back(miss.second, miss.first);
            }

            sort(sortvec.begin(), sortvec.end());

            size_t max_profiles{max_pc_profiles};
            for (auto pc_info = sortvec.rbegin(); pc_info != sortvec.rend(); pc_info++) {
                fmt::print("{:x}, {}, ", pc_info->second, pc_info->first);
                if (!--max_profiles) {
                    break;
                }
            }
        }
    }
};

static std::vector<LRUFilter> filter_set;

void print_header(bool data_only, bool ignore_compulsory)
{
    fmt::print("Bytes, ");
    if (!data_only) {
        fmt::print("I Lookups, I Misses, ");
    }
    fmt::print("D Lookups, D Misses, ");
    if (ignore_compulsory) {
        fmt::print("Non-Compulsory D Missrate\n");
    } else {
        fmt::print("D Missrate\n");
    }
}

void print_stats(bool data_only, bool ignore_compulsory)
{
    float base_dmisses = 0;
    if (ignore_compulsory) {
        base_dmisses = (float)filter_set[0].dmisses;
    }

    for (const auto& filter : filter_set) {
        fmt::print("{}, ", filter.size());
        if (!data_only) {
            fmt::print("{}, {}, ", filter.ilookups, filter.imisses);
        }
        float missrate = 0.0;
        if ((filter.dlookups - base_dmisses) > 0) {
            missrate = ((float)filter.dmisses - base_dmisses) / ((float)filter.dlookups - base_dmisses);
        }
        fmt::print("{}, {}, {}, ", filter.dlookups, filter.dmisses, missrate);
        filter.print_pc_profile();
        fmt::print("\n");
    }
}

int main(int argc, char* argv[])
{
    const uint64_t default_max_size{32768 * 1024};

    boost::optional<uint64_t> opt_end_inst, opt_max_size;
    boost::optional<size_t> opt_pc_profiling;
    boost::optional<std::string> opt_ms, opt_is;
    bool opt_warmup, opt_data_only, opt_ignore_compulsory, opt_cold;

    // TODO: add options for not resetting stats?
    po::options_description desc("Allowed options");
    desc.add_options()("help", "produce help message")("end,e",
                                                       po::value<boost::optional<uint64_t>>(&opt_end_inst),
                                                       "run working set size for 'arg' instructions from the start")(
        "max-size,m", po::value<boost::optional<uint64_t>>(&opt_max_size), "max working set size filter to test")(
        "data-only,d", po::bool_switch(&opt_data_only), "exclude code from working set size")(
        "cold,c", po::bool_switch(&opt_cold), "ignore any provided offline or online warmup")(
        "warmup,w", po::bool_switch(&opt_warmup), "report warmup working set size stats")(
        "profile,p", po::value<boost::optional<size_t>>(&opt_pc_profiling), "report PCs with most D misses")(
        "ignore-compulsory,i",
        po::bool_switch(&opt_ignore_compulsory),
        "ignore compulsory misses when reporting miss rates")(
        "ms", po::value<boost::optional<std::string>>(&opt_ms), "Memory stream file")(
        "is", po::value<boost::optional<std::string>>(&opt_is), "Instruction stream file");

    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);

    if (1 == argc || vm.count("help")) {
        std::cerr << desc << "\n";
        std::cerr << "Usage: " << std::endl;
        std::cerr << "  " << argv[0] << " [opts]" << std::endl;
        return 1;
    }

    if (opt_ignore_compulsory) {
        // 0-sized cache (unbounded) for compulsory miss checking
        filter_set.emplace_back();
    }

    auto max_size = opt_max_size ? *opt_max_size : default_max_size;

    for (uint64_t i = 1024; i <= max_size; i *= 2) {
        filter_set.emplace_back().resize(i);
    }

    if (opt_pc_profiling) {
        for (auto& filter : filter_set) {
            filter.enable_pc_profiling(*opt_pc_profiling);
        }
    }

    if (!opt_is) {
        fmt::print(stderr, "Instruction stream option required\n");
        exit(-1);
    }

    if (!opt_cold && opt_ms) {
        Access access;
        MemoryStreamReader ms_reader(*opt_ms);

        while (ms_reader.read(access)) {
            for (auto& filter : filter_set) {
                auto pc = access.pc();
                if (access.source() == Access_Source_INSTRUCTION) {
                    if (!opt_data_only) {
                        auto pc_pa = access.pa();
                        filter.lookup(pc, pc_pa, LRUFilter::LookupType::ISIDE);
                    }
                } else if (access.source() == Access_Source_DATA) {
                    auto pa = access.pa();
                    filter.lookup(pc, pa, LRUFilter::LookupType::DSIDE);
                }
            }

            access.Clear();
        }
    }

    ampere::is::Event event;
    InstructionStreamReader reader(*opt_is);

    uint64_t instruction_count = 0;
    bool in_measured = false;

    print_header(opt_data_only, opt_ignore_compulsory);

    if (opt_warmup) {
        fmt::print("Warmup Working Set:\n");
    }

    // spin through the trace and collect data
    while (reader.read(event)) {
        Instruction instruction;
        if (reader.version() >= 2) {
            if (event.has_control()) {
                if (event.control().type() == ampere::is::Control::START_MEASUREMENT) {
                    if (opt_warmup) {
                        print_stats(opt_data_only, opt_ignore_compulsory);

                        fmt::print("Measured Working Set:\n");
                    }
                    instruction_count = 0;
                    for (auto& filter : filter_set) {
                        filter.reset_stats();
                    }

                    in_measured = true;
                } else if (event.control().type() == ampere::is::Control::STOP_MEASUREMENT) {
                    break;
                }
                continue;
            }
        } else {
            in_measured = true;
        }

        if (event.has_instruction()) {
            instruction = event.instruction();
            if (!opt_cold || in_measured) {
                auto pc = instruction.program_counter().va();
                auto pc_pa = instruction.program_counter().pa();

                for (auto& filter : filter_set) {
                    if (!opt_data_only) {
                        filter.lookup(pc, pc_pa, LRUFilter::LookupType::ISIDE);
                    }
                    if (instruction.memop_size() > 0) {
                        for (int i = 0; i < instruction.memop_size(); i++) {
                            auto pa = instruction.memop(i).pa();
                            filter.lookup(pc, pa, LRUFilter::LookupType::DSIDE);
                        }
                    }
                }

                instruction_count++;

                if (in_measured && opt_end_inst && (instruction_count > *opt_end_inst))
                    break;
            }
        }

        event.Clear();
    }

    print_stats(opt_data_only, opt_ignore_compulsory);
}

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
