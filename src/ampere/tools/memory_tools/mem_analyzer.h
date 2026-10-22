//
// Copyright (c) 2018-2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <fcntl.h>
#include <iostream>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <unordered_map>
#include <cmath>
#include <fstream>

#include "ampere/instruction_stream/instruction_stream.pb.h"
#include "ampere/protobuf_utils/protobuf_utils.h"
#include "ampere/archx/decoder_input.h"

#include <google/protobuf/io/zero_copy_stream_impl.h>

#include <boost/program_options.hpp>

extern int decode_loads(const unsigned char* bytes);
extern int decode_load_pairs(const unsigned char* bytes);
extern int decode_stores(const unsigned char* bytes);

namespace ampere {
namespace tools {
namespace memory_tools {

class MemAnalyzer
{
public:
    MemAnalyzer(int argc, char const** argv);
    void Configure(int argc, char const** argv);
    void CalcStdDev(std::vector<uint64_t> arr, double& stdDev, double& mean);
    void ProcessInstr(ampere::is::Instruction instruction);
    void ProcessWindow(bool stats,
                       uint64_t& window_count,
                       std::vector<uint64_t>& global_counts,
                       std::vector<uint64_t>& window_elems,
                       std::unordered_map<std::uint64_t, std::vector<uint64_t>>& total_map);
    void StatsOutput();
    void OutputHistogram(std::vector<uint64_t>& global_counts, std::ofstream& out_f);
    void OutputStats(std::unordered_map<std::uint64_t, std::vector<uint64_t>>& total_map, std::ofstream& out_f);

    // Access functions
    uint64_t get_window_ld_pc_count() { return window_ld_pc_count; }
    uint64_t get_window_st_pc_count() { return window_st_pc_count; }
    uint64_t get_window_ldp_pc_count() { return window_ldp_pc_count; }
    uint64_t get_window_ld_addr_count() { return window_ld_addr_count; }
    uint64_t get_window_st_addr_count() { return window_st_addr_count; }
    uint64_t get_window_ldp_addr_count() { return window_ldp_addr_count; }
    std::unordered_map<std::uint64_t, std::vector<uint64_t>> get_total_ld_pc_map() { return total_ld_pc_map; }
    std::unordered_map<std::uint64_t, std::vector<uint64_t>> get_total_ld_addr_map() { return total_ld_addr_map; }
    std::unordered_map<std::uint64_t, std::vector<uint64_t>> get_total_st_pc_map() { return total_st_pc_map; }
    std::unordered_map<std::uint64_t, std::vector<uint64_t>> get_total_st_addr_map() { return total_st_addr_map; }
    std::unordered_map<std::uint64_t, std::vector<uint64_t>> get_total_ldp_pc_map() { return total_ldp_pc_map; }
    std::unordered_map<std::uint64_t, std::vector<uint64_t>> get_total_ldp_addr_map() { return total_ldp_addr_map; }
    std::vector<uint64_t> get_global_ld_pc_counts() { return global_ld_pc_counts; }
    std::vector<uint64_t> get_global_st_pc_counts() { return global_st_pc_counts; }
    std::vector<uint64_t> get_global_ldp_pc_counts() { return global_ldp_pc_counts; }
    std::vector<uint64_t> get_global_ld_addr_counts() { return global_ld_addr_counts; }
    std::vector<uint64_t> get_global_st_addr_counts() { return global_st_addr_counts; }
    std::vector<uint64_t> get_global_ldp_addr_counts() { return global_ldp_addr_counts; }

    uint64_t instruction_count{0};                                  // Total instr count
    uint64_t ld_pc_count{0}, st_pc_count{0}, ldp_pc_count{0};       // Total pc counts
    uint64_t ld_addr_count{0}, st_addr_count{0}, ldp_addr_count{0}; // Total addr counts

private:
    boost::program_options::options_description options;

    // Config parameters
    int window_size;
    bool global_stats, ldp_as_two_loads, load_pc_histogram{false}, load_pc_stats{false}, load_addr_histogram{false},
        load_addr_stats{false}, store_pc_histogram{false}, store_pc_stats{false}, store_addr_histogram{false},
        store_addr_stats{false}, ldp_pc_histogram{false}, ldp_pc_stats{false}, ldp_addr_histogram{false},
        ldp_addr_stats{false};
    std::ofstream load_pc_histogram_f, load_pc_stats_f, load_addr_histogram_f, load_addr_stats_f, store_pc_histogram_f,
        store_pc_stats_f, store_addr_histogram_f, store_addr_stats_f, ldp_pc_histogram_f, ldp_pc_stats_f,
        ldp_addr_histogram_f, ldp_addr_stats_f;

    uint64_t window_ld_pc_count{0}, window_st_pc_count{0}, window_ldp_pc_count{0};       // window pc counts
    uint64_t window_ld_addr_count{0}, window_st_addr_count{0}, window_ldp_addr_count{0}; // window address counts

    std::unordered_map<std::uint64_t, std::vector<uint64_t>> total_ld_pc_map;   // LD PC map
    std::unordered_map<std::uint64_t, std::vector<uint64_t>> total_ld_addr_map; // LD ADDR map
    std::vector<std::uint64_t> window_ld_pcs, window_ld_addrs;

    std::unordered_map<std::uint64_t, std::vector<uint64_t>> total_st_pc_map;   // ST PC map
    std::unordered_map<std::uint64_t, std::vector<uint64_t>> total_st_addr_map; // ST ADDR map
    std::vector<std::uint64_t> window_st_pcs, window_st_addrs;

    std::unordered_map<std::uint64_t, std::vector<uint64_t>> total_ldp_pc_map;   // LDP PC map
    std::unordered_map<std::uint64_t, std::vector<uint64_t>> total_ldp_addr_map; // LDP ADDR map
    std::vector<std::uint64_t> window_ldp_pcs, window_ldp_addrs;

    std::vector<uint64_t>
        global_ld_pc_counts; // (2 * window_size + 1, 0);  // if LDP counts as 2 uops, LDs can be 2x window
    std::vector<uint64_t> global_st_pc_counts;  // (window_size + 1, 0);
    std::vector<uint64_t> global_ldp_pc_counts; // (window_size + 1, 0);

    std::vector<uint64_t> global_ld_addr_counts;  // (2 * window_size + 1, 0);
    std::vector<uint64_t> global_st_addr_counts;  // (2 * window_size + 1, 0);
    std::vector<uint64_t> global_ldp_addr_counts; // (2 * window_size + 1, 0);
};

} // namespace memory_tools
} // namespace tools
} // namespace ampere

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
