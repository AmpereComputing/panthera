//
// Copyright (c) 2018-2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/tools/memory_tools/mem_analyzer.h"

namespace ampere {
namespace tools {
namespace memory_tools {

MemAnalyzer::MemAnalyzer(int argc, char const** argv)
{
    boost::program_options::options_description options;
    Configure(argc, argv);

    global_ld_pc_counts.assign(2 * window_size + 1, 0); // if LDP counts as 2 uops, LDs can be 2x window
    global_st_pc_counts.assign(window_size + 1, 0);
    global_ldp_pc_counts.assign(window_size + 1, 0);

    global_ld_addr_counts.assign(2 * window_size + 1, 0);
    global_st_addr_counts.assign(2 * window_size + 1, 0);
    global_ldp_addr_counts.assign(2 * window_size + 1, 0);
} // Constructor

// Command-line options
void MemAnalyzer::Configure(int argc, char const** argv)
{
    options.add_options()("help", "Input stream from stdin")(
        "window_size",
        boost::program_options::value<int>(&window_size)->default_value(192),
        "Analysis instruction window size")("global_stats",
                                            boost::program_options::value<bool>(&global_stats)->default_value(true),
                                            "Print global instruction window stats")(
        "ldp_as_two_loads",
        boost::program_options::value<bool>(&ldp_as_two_loads)->default_value(true),
        "Counts load-pair instructions as two load operations")

        ("load_pc_histogram",
         boost::program_options::value<std::string>(),
         "Prints load pc histogram to specified "
         "filename")("load_pc_stats",
                     boost::program_options::value<std::string>(),
                     "Prints load pc stats per unique load pc to "
                     "specified filename")("load_addr_histogram",
                                           boost::program_options::value<std::string>(),
                                           "Prints load address histogram to "
                                           "specified filename")("load_addr_stats",
                                                                 boost::program_options::value<std::string>(),
                                                                 "Prints load address stats per unique load "
                                                                 "address to specified filename")

            ("store_pc_histogram",
             boost::program_options::value<std::string>(),
             "Prints store pc histrogram to specified "
             "filename")("store_pc_stats",
                         boost::program_options::value<std::string>(),
                         "Prints store pc stats per unique store pc to "
                         "specified filename")("store_addr_histogram",
                                               boost::program_options::value<std::string>(),
                                               "Prints store address histogram to "
                                               "specified filename")("store_addr_stats",
                                                                     boost::program_options::value<std::string>(),
                                                                     "Prints store address stats per unique store"
                                                                     " address to specified filename")

                ("ldp_pc_histogram",
                 boost::program_options::value<std::string>(),
                 "Prints ldp pc histogram to specified "
                 "filename")("ldp_pc_stats",
                             boost::program_options::value<std::string>(),
                             "Prints ldp pc stats per unique ldp pc to "
                             "specified filename")("ldp_addr_histogram",
                                                   boost::program_options::value<std::string>(),
                                                   "Prints ldp address histogram to specified"
                                                   " filename")("ldp_addr_stats",
                                                                boost::program_options::value<std::string>(),
                                                                "Prints ldp address stats per unique ldp "
                                                                "address to specified filename");

    boost::program_options::variables_map vm;
    try {
        boost::program_options::store(boost::program_options::parse_command_line(argc, argv, options), vm);
        boost::program_options::notify(vm);
    } catch (std::exception& e) {
        std::cout << std::endl << e.what() << std::endl;
        std::cout << options << std::endl;
        throw(std::runtime_error("Bad parameters"));
    }

    if (vm.count("help") || (argc == 1)) {
        std::cout << std::endl << "Usage: xzcat <instr_stream> | " << argv[0] << " [options]: " << std::endl;
        std::cout << options << std::endl;
        throw(std::runtime_error("Bad parameters"));
    }
    if (vm.count("window_size")) {
        window_size = vm["window_size"].as<int>();
    }
    if (vm.count("global_stats")) {
        global_stats = vm["global_stats"].as<bool>();
    }
    if (vm.count("ldp_as_two_loads")) {
        ldp_as_two_loads = vm["ldp_as_two_loads"].as<bool>();
    }

    if (vm.count("load_pc_histogram")) {
        load_pc_histogram = true;
        load_pc_histogram_f.open(vm["load_pc_histogram"].as<std::string>());
    }
    if (vm.count("load_pc_stats")) {
        load_pc_stats = true;
        load_pc_stats_f.open(vm["load_pc_stats"].as<std::string>());
    }
    if (vm.count("load_addr_histogram")) {
        load_addr_histogram = true;
        load_addr_histogram_f.open(vm["load_addr_histogram"].as<std::string>());
    }
    if (vm.count("load_addr_stats")) {
        load_addr_stats = true;
        load_addr_stats_f.open(vm["load_addr_stats"].as<std::string>());
    }

    if (vm.count("store_pc_histogram")) {
        store_pc_histogram = true;
        store_pc_histogram_f.open(vm["store_pc_histogram"].as<std::string>());
    }
    if (vm.count("store_pc_stats")) {
        store_pc_stats = true;
        store_pc_stats_f.open(vm["store_pc_stats"].as<std::string>());
    }
    if (vm.count("store_addr_histogram")) {
        store_addr_histogram = true;
        store_addr_histogram_f.open(vm["store_addr_histogram"].as<std::string>());
    }
    if (vm.count("store_addr_stats")) {
        store_addr_stats = true;
        store_addr_stats_f.open(vm["store_addr_stats"].as<std::string>());
    }

    if (vm.count("ldp_pc_histogram")) {
        ldp_pc_histogram = true;
        ldp_pc_histogram_f.open(vm["ldp_pc_histogram"].as<std::string>());
    }
    if (vm.count("ldp_pc_stats")) {
        ldp_pc_stats = true;
        ldp_pc_stats_f.open(vm["ldp_pc_stats"].as<std::string>());
    }
    if (vm.count("ldp_addr_histogram")) {
        ldp_addr_histogram = true;
        ldp_addr_histogram_f.open(vm["ldp_addr_histogram"].as<std::string>());
    }
    if (vm.count("ldp_addr_stats")) {
        ldp_addr_stats = true;
        ldp_addr_stats_f.open(vm["ldp_addr_stats"].as<std::string>());
    }
} // Configure

void MemAnalyzer::ProcessWindow(bool stats,
                                uint64_t& window_count,
                                std::vector<uint64_t>& global_counts,
                                std::vector<uint64_t>& window_elems,
                                std::unordered_map<std::uint64_t, std::vector<uint64_t>>& total_map)
{
    //  Update stats structure
    if (stats) {
        for (auto i : window_elems) {
            if (total_map[i].size() < window_count) {
                total_map[i].resize(window_count, 0);
            }
            total_map[i].at(window_count - 1)++;
        }

        window_elems.clear();
    }

    // Update histogram structure
    global_counts[window_count]++;
    window_count = 0;
} // ProcessWindow

// Process each input instr to collect stats at global and window granularity
void MemAnalyzer::ProcessInstr(ampere::is::Instruction instruction)
{
    archx::DecoderInput di(instruction.opcode());

    // Process loads
    if (decode_loads(di.ptr_no_mode())) {
        // Process load-pairs
        if (decode_load_pairs(di.ptr_no_mode())) {
            ++ldp_pc_count;
            ++window_ldp_pc_count;
            window_ldp_pcs.push_back(instruction.program_counter().va());
            if (ldp_addr_histogram || ldp_addr_stats) {
                for (int i = 0; i < instruction.memop_size(); i++) {
                    ++ldp_addr_count;
                    ++window_ldp_addr_count;
                    window_ldp_addrs.push_back(instruction.memop(i).va());
                }
            }
            if (ldp_as_two_loads) {
                ++ld_pc_count;
                ++window_ld_pc_count;
            }
        }
        ++ld_pc_count;
        ++window_ld_pc_count;
        window_ld_pcs.push_back(instruction.program_counter().va());

        if (load_addr_histogram || load_addr_stats) {
            for (int i = 0; i < instruction.memop_size(); i++) {
                ++ld_addr_count;
                ++window_ld_addr_count;
                window_ld_addrs.push_back(instruction.memop(i).va());
            }
        }
    }

    // Process stores
    if (decode_stores(di.ptr_no_mode())) {
        ++st_pc_count;
        ++window_st_pc_count;
        window_st_pcs.push_back(instruction.program_counter().va());

        if (store_addr_histogram || store_addr_stats) {
            for (int i = 0; i < instruction.memop_size(); i++) {
                ++st_addr_count;
                ++window_st_addr_count;
                window_st_addrs.push_back(instruction.memop(i).va());
            }
        }
    }

    ++instruction_count;

    // Update global stats & pc/addr counts and reset window counts
    if ((instruction_count % window_size) == 0) {
        ProcessWindow(load_pc_stats, window_ld_pc_count, global_ld_pc_counts, window_ld_pcs, total_ld_pc_map);
        ProcessWindow(load_addr_stats, window_ld_addr_count, global_ld_addr_counts, window_ld_addrs, total_ld_addr_map);
        ProcessWindow(store_pc_stats, window_st_pc_count, global_st_pc_counts, window_st_pcs, total_st_pc_map);
        ProcessWindow(
            store_addr_stats, window_st_addr_count, global_st_addr_counts, window_st_addrs, total_st_addr_map);
        ProcessWindow(ldp_pc_stats, window_ldp_pc_count, global_ldp_pc_counts, window_ldp_pcs, total_ldp_pc_map);
        ProcessWindow(
            ldp_addr_stats, window_ldp_addr_count, global_ldp_addr_counts, window_ldp_addrs, total_ldp_addr_map);
    }
} // ProcessInstr

void MemAnalyzer::OutputHistogram(std::vector<uint64_t>& global_counts, std::ofstream& out_f)
{
    double stddev = 0.0, mean = 0.0;

    uint64_t max = 0, min = 0;
    size_t max_bucket = 0, min_bucket = global_counts.size();

    for (size_t i = 0; i < global_counts.size(); i++) {
        out_f << i << "," << global_counts[i] << std::endl;

        if ((global_counts[i] > 0) && (i > max_bucket)) {
            max_bucket = i;
            max = global_counts[i];
        }
        // Minimum non-zero bucket and don't count bucket 0
        if ((global_counts[i] > 0) && (i != 0) && (i < min_bucket)) {
            min_bucket = i;
            min = global_counts[i];
        }
    }

    CalcStdDev(global_counts, stddev, mean);

    std::cout << "    mean, " << mean << std::endl
              << "    stddev, " << stddev << std::endl
              << "    window_max, " << max_bucket << std::endl
              << "    window_max_num, " << max << std::endl
              << "    window_min, " << min_bucket << std::endl
              << "    window_min_num, " << min << std::endl;
} // OutputHistogram

void MemAnalyzer::OutputStats(std::unordered_map<std::uint64_t, std::vector<uint64_t>>& total_map, std::ofstream& out_f)
{
    for (auto elem : total_map) {
        out_f << std::hex << elem.first << std::dec << ", ";
        for (auto count : elem.second)
            out_f << count << ", ";
        out_f << std::endl;
    }
} // OutputStats

void MemAnalyzer::StatsOutput()
{
    if (global_stats) {
        std::cout << std::endl
                  << "instruction count, " << instruction_count << std::endl
                  << "load count, " << ld_pc_count << std::endl
                  << "ldp count, " << ldp_pc_count << std::endl
                  << "store count, " << st_pc_count << std::endl
                  << "size, " << window_size << std::endl;
    }

    if (load_pc_histogram || load_addr_histogram || load_pc_stats || load_addr_stats) {
        std::cout << "loads" << std::endl;
        if (load_pc_histogram) {
            std::cout << "  PC" << std::endl;
            OutputHistogram(global_ld_pc_counts, load_pc_histogram_f);
        }
        if (load_addr_histogram) {
            std::cout << "  ADDR" << std::endl;
            OutputHistogram(global_ld_addr_counts, load_addr_histogram_f);
        }

        if (load_pc_stats) {
            std::cout << "  unique_load_pcs, " << total_ld_pc_map.size() << std::endl;
            OutputStats(total_ld_pc_map, load_pc_stats_f);
        }
        if (load_addr_stats) {
            std::cout << "  unique_load_addrs, " << total_ld_addr_map.size() << std::endl;
            OutputStats(total_ld_addr_map, load_addr_stats_f);
        }
    }

    if (store_pc_histogram || store_addr_histogram || store_pc_stats || store_addr_stats) {
        std::cout << "stores" << std::endl;
        if (store_pc_histogram) {
            std::cout << "  PC" << std::endl;
            OutputHistogram(global_st_pc_counts, store_pc_histogram_f);
        }
        if (store_addr_histogram) {
            std::cout << "  ADDR" << std::endl;
            OutputHistogram(global_st_addr_counts, store_addr_histogram_f);
        }

        if (store_pc_stats) {
            std::cout << "  unique_store_pcs, " << total_st_pc_map.size() << std::endl;
            OutputStats(total_st_pc_map, store_pc_stats_f);
        }
        if (store_addr_stats) {
            std::cout << "  unique_store_addrs, " << total_st_addr_map.size() << std::endl;
            OutputStats(total_st_addr_map, store_addr_stats_f);
        }
    }

    if (ldp_pc_histogram || ldp_addr_histogram || ldp_pc_stats || ldp_addr_stats) {
        std::cout << "load-pairs" << std::endl;
        if (ldp_pc_histogram) {
            std::cout << "  PC" << std::endl;
            OutputHistogram(global_ldp_pc_counts, ldp_pc_histogram_f);
        }
        if (ldp_addr_histogram) {
            std::cout << "  ADDR" << std::endl;
            OutputHistogram(global_ldp_addr_counts, ldp_addr_histogram_f);
        }

        if (ldp_pc_stats) {
            std::cout << "  unique_ldp_pcs, " << total_ldp_pc_map.size() << std::endl;
            OutputStats(total_ldp_pc_map, ldp_pc_stats_f);
        }
        if (ldp_addr_stats) {
            std::cout << "  unique_ldp_addrs, " << total_ldp_addr_map.size() << std::endl;
            OutputStats(total_ldp_addr_map, ldp_addr_stats_f);
        }
    }

} // StatsOutput

void MemAnalyzer::CalcStdDev(std::vector<uint64_t> arr, double& stdDev, double& mean)
{
    double sum = 0.0;
    uint64_t i;

    for (i = 0; i < arr.size(); i++) {
        sum += (double)arr[i];
    }
    mean = sum / arr.size();

    for (i = 0; i < arr.size(); i++)
        stdDev += pow(arr[i] - mean, 2);
    stdDev = sqrt(stdDev / arr.size());
} // CalcStdDev

} // namespace memory_tools
} // namespace tools
} // namespace ampere

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
