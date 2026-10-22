//
// Copyright (c) 2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include <boost/filesystem.hpp>
#include <boost/program_options.hpp>

#include "ampere/archx/branch_type.h"
#include "ampere/archx/driver_is.h"
#include "ampere/core/branch_history.h"
#include "ampere/core/fetch_id.h"

namespace fs = boost::filesystem;
namespace po = boost::program_options;

using namespace ampere;
using namespace ampere::archx;
using namespace ampere::core;

using Stats = std::map<size_t, std::map<std::uint64_t, std::set<std::uint64_t>>>;

void usage(const std::string& prog)
{
    std::string program_name = fs::path{prog}.filename().native();
    std::cerr << "usage: " << program_name << " <instruction stream>" << std::endl;
    exit(-1);
}

void push_history(BranchHistory& history,
                  FetchId fetch_id,
                  BranchType branch_type,
                  bool taken,
                  std::shared_ptr<Instruction>& instruction)
{
    if (is_conditional(branch_type) && taken)
        history.push(fetch_id, instruction->program_counter());
    else if (is_indirect(branch_type))
        history.push(fetch_id, instruction->program_counter(), instruction->next_program_counter());
    else if (is_unconditional(branch_type))
        history.push(fetch_id, instruction->program_counter());
}

Stats run(const fs::path& stream, std::uint64_t max_instructions, size_t hash_bits, size_t reference_hash_bits)
{
    Stats stats;
    DriverIS driver(stream.native());
    auto reader = new is::InstructionStreamReader(stream);

    std::shared_ptr<Instruction> instruction;
    std::uint64_t instructions{0};
    FetchId fetch_id{1};

    BranchHistory reference_history{"reference_history"};
    BranchHistory history{"history"};
    static size_t history_sizes[] = {1, 2, 3, 4, 7, 11, 18, 29, 47, 76, 123, 199};

    for (auto size : history_sizes) {
        reference_history.add_history_size(size);
        history.add_history_size(size);
    }

    while ((instruction = driver.fetch(driver.next_pc()))) {
        auto branch_type = instruction->branch_type();

        if (branch_type != BranchType::NONE) {
            bool non_seq = instruction->next_program_counter() != instruction->program_counter() + 4;
            bool taken = is_unconditional(branch_type) || (is_conditional(branch_type) && non_seq);

            push_history(reference_history, fetch_id, branch_type, taken, instruction);
            push_history(history, fetch_id, branch_type, taken, instruction);

            reference_history.start_hashing(fetch_id);
            history.start_hashing(fetch_id);

            for (auto depth : history_sizes) {
                auto href = reference_history.hash(depth, reference_hash_bits);
                auto h = history.hash(depth, hash_bits);
                stats[depth][h].insert(href);
            }

            reference_history.commit(fetch_id);
            history.commit(fetch_id);
            ++fetch_id;
        }

        driver.commit(instruction);

        ++instructions;

        if (instructions >= max_instructions
            || (instruction->control() && *instruction->control() == SimulationControl::STOP_MEASUREMENT))
        {
            break;
        }
    }

    return stats;
}

std::string extract_trace_name(fs::path p)
{
    if (p.has_extension()) {
        while (p.extension() != ".is")
            p = p.stem();

        if (p.has_extension())
            p = p.stem();
    }

    return p.native();
}

class RunningStats
{
public:
    RunningStats() { clear(); }

    void clear() { n = 0; }

    void push(double x)
    {
        n += 1;

        if (n == 1) {
            M = Mprev = x;
            Sprev = 0.0;
        } else {
            M = Mprev + (x - Mprev) / n;
            S = Sprev + (x - Mprev) * (x - M);

            Mprev = M;
            Sprev = S;
        }
    }

    double mean() const { return n ? M : 0.0; }
    double variance() const { return (n > 1) ? (S / (n - 1)) : 0.0; }

private:
    std::uint64_t n;
    double M, Mprev, S, Sprev;
};

void print_stats(const std::string& trace_name, const Stats& stats)
{
    RunningStats summary;

    std::cout << "trace,depth,alias_avg,alias_var\n";

    for (auto& histo_per_depth : stats) {
        RunningStats per_depth_summary;

        for (auto& histo : histo_per_depth.second) {
            per_depth_summary.push(histo.second.size());
            summary.push(histo.second.size());
        }

        std::cout << trace_name << ',' << histo_per_depth.first << ',' << per_depth_summary.mean() << ','
                  << per_depth_summary.variance() << "\n";
    }

    std::cout << trace_name << ",-," << summary.mean() << ',' << summary.variance() << std::endl;
}

int main(int argc, char** argv)
{
    fs::path instruction_stream;
    std::string trace_name{};
    std::uint64_t max_instructions = std::numeric_limits<std::uint64_t>::max();
    std::string hash_type{"FNVHasher"};
    std::string reference_hash_type{"FNVHasher"};
    size_t hash_bits{20};
    size_t reference_hash_bits{64};

    po::options_description options("Options");
    options.add_options()("help,h", "print help")(
        "max_instructions,i", po::value<std::uint64_t>(&max_instructions), "max instructions to simulate")(
        "csv", "print output in CSV format")("trace,t",
                                             po::value<std::string>(&trace_name),
                                             "trace name, if it cannot be derived from the instruction stream name")(
        "instruction_stream", po::value<fs::path>(&instruction_stream)->required(), "instruction stream name")(
        "hash", po::value<std::string>(&hash_type), "hash type for the experiment")(
        "reference_hash", po::value<std::string>(&reference_hash_type), "hash type for the reference")(
        "hash_bits", po::value<size_t>(&hash_bits), "number of bits in the experimental hash output")(
        "reference_hash_bits", po::value<size_t>(&reference_hash_bits), "number of bits in the reference hash output");

    po::positional_options_description positional;
    positional.add("instruction_stream", 1);

    po::variables_map vm;

    try {
        po::store(po::command_line_parser(argc, argv).options(options).positional(positional).run(), vm);

        if (vm.count("help"))
            usage(argv[0]);

        po::notify(vm);
    } catch (po::error& err) {
        usage(argv[0]);
    }

    auto& parameters = sc::Environment::get_root()->parameters;
    parameters.add_setting("history.hash.type", hash_type, "is_branch_hash");
    parameters.add_setting("reference_history.hash.type", reference_hash_type, "is_branch_hash");

    auto stats = run(instruction_stream, max_instructions, hash_bits, reference_hash_bits);

    if (trace_name.empty())
        trace_name = extract_trace_name(instruction_stream);

    print_stats(trace_name, stats);

    return 0;
}
