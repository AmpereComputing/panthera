//
// Copyright (c) 2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include <boost/filesystem.hpp>
#include <boost/program_options.hpp>
#include <stack>
#include <cstdlib>

#include "fmt/format.h"

#include "ampere/archx/branch_type.h"
#include "ampere/archx/driver_is.h"
#include "ampere/core/branch_history.h"

#include "chip-decoder-map.h"

namespace fs = boost::filesystem;
namespace po = boost::program_options;

using namespace std;
using namespace ampere;
using namespace ampere::archx;
using namespace ampere::core;

size_t hash_pc(ProgramCounter pc)
{
    auto x = pc.extract_range(51, 6);

    x ^= x >> 32;
    x ^= x >> 16;
    x ^= x >> 8;
    x ^= x >> 4;

    return (x ^ pc.extract_range(5, 2)) & 0b1111;
}

unsigned hash_target(ProgramCounter pc)
{
    auto x = pc.extract_range(51, 2);

    x ^= x >> 32;
    x ^= x >> 16;
    x ^= x >> 8;
    x ^= x >> 4;
    x ^= x >> 2;

    return x & 0b11;
}

struct BranchInfo
{
    ProgramCounter branch_pc;
};

class PCLastSeenTracker
{
public:
    PCLastSeenTracker(size_t sets) : num_sets{sets}, last_seen(sets, 0) {}
    void saw_branch(unsigned idx)
    {
        for (unsigned i = 0; i < num_sets; i++) {
            last_seen[i] = (last_seen[i] + 1) % 0b1111;
        }
        last_seen[idx] = 0;
    }

    const size_t num_sets;
    std::vector<uint64_t> last_seen;
};

class LoopTracker
{
public:
    LoopTracker(size_t sets) : num_sets{sets}, loop_index(sets, 0), pcs(sets, 0x0_pc) {}
    void saw_branch(unsigned idx, ProgramCounter pc)
    {
        if (pcs[idx] == pc) {
            loop_index[idx]++;
        } else {
            loop_index[idx] = 0;
            pcs[idx] = pc;
        }
    }

    const size_t num_sets;
    std::vector<uint64_t> loop_index;
    std::vector<ProgramCounter> pcs;
};

#define tf(var) (var ? 1 : 0)

void run(const fs::path& stream, uint64_t max_instructions, bool include_counts)
{
    vector<string> decoder_names = is_tools::get_decoders("max");
    archx::Instruction::set_decoders(decoder_names);
    DriverIS driver(stream.native());

    shared_ptr<Instruction> instruction;
    bool measuring{false};
    uint64_t instructions{0}, branches{0}, taken_branches{0};
    ProgramCounter entry_point{0x0_pc};
    stack<ProgramCounter> return_stack;
    uint64_t imli{0};
    VirtAddr last_load_va{0x0_va};
    PCLastSeenTracker tracker(16);
    LoopTracker loop_tracker(16);

    if (include_counts)
        fmt::print("instructions,branches,taken_branches,");
    fmt::print(
        "entry_point,last_seen_0,last_seen_1,last_seen_2,last_seen_3,last_seen_4,last_seen_5,last_seen_6,last_seen_7,"
        "last_seen_8,last_seen_9,last_seen_10,last_seen_11,last_seen_12,last_seen_13,last_seen_14,last_seen_15,last_"
        "load_va,branch_pc,imli,loop_tracker_0,loop_tracker_1,loop_tracker_2,loop_tracker_3,loop_tracker_4,loop_"
        "tracker_5,loop_tracker_6,loop_tracker_7,loop_tracker_8,loop_tracker_9,loop_tracker_10,loop_tracker_11,loop_"
        "tracker_12,loop_tracker_13,loop_tracker_14,loop_tracker_15,return_stack,is_direct,is_conditional,is_call,is_"
        "return,is_backwards_direct,target,taken\n");

    while ((instruction = driver.fetch(driver.next_pc()))) {
        bool non_sequential = instruction->next_program_counter() != instruction->program_counter() + 4;
        // Only count/print branches found in the measurement section
        auto control = instruction->control();
        if (control && *control == SimulationControl::START_MEASUREMENT) {
            measuring = true;
        } else if (!measuring) {
            if (non_sequential) {
                entry_point = instruction->next_program_counter();
            }
            driver.commit(instruction);
            continue;
        }

        auto branch_type = instruction->branch_type();

        if (branch_type != BranchType::NONE) {
            ProgramCounter pc = instruction->program_counter();
            bool taken_branch = instruction->is_taken_branch();
            ProgramCounter target = taken_branch ? instruction->branch_target() : instruction->next_program_counter();

            bool backwards_direct = is_direct(branch_type) && branch_target_if_taken(pc, instruction->opcode()) < pc;

            if (include_counts) {
                fmt::print("{},{},{},", instructions, branches, taken_branches);
            }
            fmt::print("{},{},{},{},{},{},{},{},{},{},{},{},{},{}\n",
                       entry_point.value(),
                       fmt::join(tracker.last_seen, ","),
                       last_load_va.value(),
                       pc.value(),
                       imli,
                       fmt::join(loop_tracker.loop_index, ","),
                       return_stack.empty() ? 0 : return_stack.top().value(),
                       tf(is_direct(branch_type)),
                       tf(is_conditional(branch_type)),
                       tf(is_call(branch_type)),
                       tf(is_return(branch_type)),
                       tf(backwards_direct), // is_backwards_direct
                       target.value(),
                       tf(taken_branch));

            if (taken_branch) {
                ++taken_branches;
                if (is_call(branch_type)) {
                    return_stack.push(pc + 4);
                } else if (is_return(branch_type) && !return_stack.empty()) {
                    return_stack.pop();
                }

                size_t this_branch_hash = hash_pc(pc);
                if (is_indirect(branch_type))
                    this_branch_hash ^= hash_target(target);
                tracker.saw_branch(this_branch_hash);

                if (backwards_direct) {
                    loop_tracker.saw_branch(this_branch_hash, pc);
                }
            }
            ++branches;
        } else if (instruction->is_load() && instruction->num_addresses() > 0) {
            last_load_va = instruction->virtual_address();
        }

        if (non_sequential) {
            entry_point = instruction->next_program_counter();
        }

        driver.commit(instruction);

        ++instructions;

        if (instructions >= max_instructions || (control && *control == SimulationControl::STOP_MEASUREMENT)) {
            break;
        }
    }
}

int main(int argc, char** argv)
{
    bool include_counts{false};
    fs::path instruction_stream;
    string trace_name{};
    uint64_t max_instructions = numeric_limits<uint64_t>::max();

    po::options_description options("Options");
    options.add_options()("help,h", "print help")(
        "max_instructions,i", po::value<uint64_t>(&max_instructions), "max instructions to simulate")(
        "include_counts", po::bool_switch(&include_counts), "include instruction/branch counts")(
        "csv", "print output in CSV format")(
        "instruction_stream", po::value<fs::path>(&instruction_stream)->required(), "instruction stream name");

    po::positional_options_description positional;
    positional.add("instruction_stream", 1);

    po::variables_map vm;

    try {
        po::store(po::command_line_parser(argc, argv).options(options).positional(positional).run(), vm);

        if (vm.count("help")) {
            cout << options << endl;
            exit(-1);
        }

        po::notify(vm);
    } catch (po::error& err) {
        cout << options << endl;
        exit(-1);
    }

    run(instruction_stream, max_instructions, include_counts);

    return 0;
}
