//
// Copyright (c) 2018-2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <gtest/gtest.h>
#include "ampere/tools/memory_tools/mem_analyzer.h"

namespace ampere {
namespace tools {
using namespace memory_tools;

class MemAnalyzerTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        fd = open(this->test_file_name(), O_CREAT | O_RDWR | O_TRUNC, S_IRUSR | S_IWUSR);
        ASSERT_GT(fd, 2);
    }

    void TearDown() override { ASSERT_EQ(0, unlink(this->test_file_name())); }

    const char *test_file_name()
    {
        const ::testing::TestInfo *const test_info = ::testing::UnitTest::GetInstance()->current_test_info();
        return test_info->name();
    }

    int fd;
};

TEST_F(MemAnalyzerTest, testConstructor)
{
    char const *arg[] = {"mem_analyzer", "--global_stats", "1"};
    ampere::tools::memory_tools::MemAnalyzer memAnalyzer(3, arg);
}

TEST_F(MemAnalyzerTest, testProcessInstrLD)
{
    char const *arg[] = {"mem_analyzer", "--window_size", "2"};
    ampere::tools::memory_tools::MemAnalyzer memAnalyzer(3, arg);

    ampere::is::Instruction instruction;
    instruction.set_opcode(0b00011000000000000000000000000000); // LDR

    memAnalyzer.ProcessInstr(instruction);
    ASSERT_EQ(1, memAnalyzer.instruction_count);
    ASSERT_EQ(1, memAnalyzer.ld_pc_count);
    ASSERT_EQ(0, memAnalyzer.ldp_pc_count);
    ASSERT_EQ(0, memAnalyzer.st_pc_count);
}

TEST_F(MemAnalyzerTest, testProcessInstrLDADDR)
{
    char const *arg[] = {"mem_analyzer", "--window_size", "2", "--load_addr_histogram", this->test_file_name()};
    ampere::tools::memory_tools::MemAnalyzer memAnalyzer(5, arg);

    ampere::is::Instruction instruction;
    instruction.set_opcode(0b00011000000000000000000000000000); // LDR
    instruction.add_memop()->set_va(0xdeadbeef);

    memAnalyzer.ProcessInstr(instruction);
    ASSERT_EQ(1, memAnalyzer.ld_addr_count);
    ASSERT_EQ(0, memAnalyzer.ldp_addr_count);
    ASSERT_EQ(0, memAnalyzer.st_addr_count);
}

TEST_F(MemAnalyzerTest, testProcessInstrLDP)
{
    char const *arg[] = {"mem_analyzer", "--window_size", "2"};
    ampere::tools::memory_tools::MemAnalyzer memAnalyzer(3, arg);

    ampere::is::Instruction instruction;
    instruction.set_opcode(0b00101000110000000000000000000000); // LDP

    memAnalyzer.ProcessInstr(instruction);
    ASSERT_EQ(1, memAnalyzer.instruction_count);
    ASSERT_EQ(2, memAnalyzer.ld_pc_count); // Test ldp_as_two_loads true
    ASSERT_EQ(1, memAnalyzer.ldp_pc_count);
    ASSERT_EQ(0, memAnalyzer.st_pc_count);
}

TEST_F(MemAnalyzerTest, testProcessInstrLDPADDR)
{
    char const *arg[] = {"mem_analyzer", "--window_size", "2", "--ldp_addr_histogram", this->test_file_name()};
    ampere::tools::memory_tools::MemAnalyzer memAnalyzer(5, arg);

    ampere::is::Instruction instruction;
    instruction.set_opcode(0b00101000110000000000000000000000); // LDP
    instruction.add_memop()->set_va(0xaaaabbbb);
    instruction.add_memop()->set_va(0xccccdddd);

    memAnalyzer.ProcessInstr(instruction);
    ASSERT_EQ(0, memAnalyzer.ld_addr_count);
    ASSERT_EQ(2, memAnalyzer.ldp_addr_count);
    ASSERT_EQ(0, memAnalyzer.st_addr_count);
}

TEST_F(MemAnalyzerTest, testProcessInstrST)
{
    char const *arg[] = {"mem_analyzer", "--window_size", "2"};
    ampere::tools::memory_tools::MemAnalyzer memAnalyzer(3, arg);

    ampere::is::Instruction instruction;
    instruction.set_opcode(0b00111100000000000000010000000000); // STR

    memAnalyzer.ProcessInstr(instruction);
    ASSERT_EQ(1, memAnalyzer.instruction_count);
    ASSERT_EQ(0, memAnalyzer.ld_pc_count);
    ASSERT_EQ(0, memAnalyzer.ldp_pc_count);
    ASSERT_EQ(1, memAnalyzer.st_pc_count);
}

TEST_F(MemAnalyzerTest, testProcessInstrSTADDR)
{
    char const *arg[] = {"mem_analyzer", "--window_size", "2", "--store_addr_histogram", this->test_file_name()};
    ampere::tools::memory_tools::MemAnalyzer memAnalyzer(5, arg);

    ampere::is::Instruction instruction;
    instruction.set_opcode(0b00111100000000000000010000000000); // STR
    instruction.add_memop()->set_va(0xdeadbeef);

    memAnalyzer.ProcessInstr(instruction);
    ASSERT_EQ(0, memAnalyzer.ld_addr_count);
    ASSERT_EQ(0, memAnalyzer.ldp_addr_count);
    ASSERT_EQ(1, memAnalyzer.st_addr_count);
}

TEST_F(MemAnalyzerTest, testProcessWindow)
{
    char const *arg[] = {"mem_analyzer", "--window_size", "8"};
    ampere::tools::memory_tools::MemAnalyzer memAnalyzer(3, arg);

    uint64_t num;
    std::vector<uint64_t> counts = {1, 2, 3, 4, 5, 6, 7, 8, 9};
    std::vector<uint64_t> elems = {0x11111111, 0x22222222, 0x33333333, 0x44444444};
    std::unordered_map<std::uint64_t, std::vector<uint64_t>> maps;
    maps[0x11111111] = {0, 1};
    maps[0x22222222] = {1, 2, 3};

    // Test global count
    num = elems.size();
    ASSERT_EQ(5, counts[elems.size()]);
    memAnalyzer.ProcessWindow(false, num, counts, elems, maps);
    ASSERT_EQ(0, num);                  // Zero out window count
    ASSERT_EQ(6, counts[elems.size()]); // Increment histogram bucket count

    // Test map counts
    num = elems.size();
    ASSERT_EQ(2, maps.size());             // Initial map check
    ASSERT_EQ(2, maps[0x11111111].size()); // Initial map check
    ASSERT_EQ(3, maps[0x22222222].size()); // Initial map check

    memAnalyzer.ProcessWindow(true, num, counts, elems, maps);

    ASSERT_EQ(0, num);          // Zero out window count
    ASSERT_EQ(0, elems.size()); // Zero out window elements (PCs/ADDRs)
    ASSERT_EQ(4, maps.size());  // Map add elements size check

    // Map element counts size check
    ASSERT_EQ(4, maps[0x11111111].size());
    ASSERT_EQ(4, maps[0x22222222].size());
    ASSERT_EQ(4, maps[0x33333333].size());
    ASSERT_EQ(4, maps[0x44444444].size());

    // Map element count check
    ASSERT_EQ(0, maps[0x11111111][0]);
    ASSERT_EQ(1, maps[0x11111111][1]);
    ASSERT_EQ(0, maps[0x11111111][2]);
    ASSERT_EQ(1, maps[0x11111111][3]);

    ASSERT_EQ(1, maps[0x22222222][0]);
    ASSERT_EQ(2, maps[0x22222222][1]);
    ASSERT_EQ(3, maps[0x22222222][2]);
    ASSERT_EQ(1, maps[0x22222222][3]);

    ASSERT_EQ(0, maps[0x33333333][0]);
    ASSERT_EQ(0, maps[0x33333333][1]);
    ASSERT_EQ(0, maps[0x33333333][2]);
    ASSERT_EQ(1, maps[0x33333333][3]);
}

TEST_F(MemAnalyzerTest, testProcessInstrWindow)
{
    char const *arg[] = {"mem_analyzer",         "--window_size",         "2",
                         "--load_pc_histogram",  this->test_file_name(),  "--load_pc_stats",
                         this->test_file_name(), "--load_addr_histogram", this->test_file_name(),
                         "--load_addr_stats",    this->test_file_name(),  "--ldp_pc_histogram",
                         this->test_file_name(), "--ldp_pc_stats",        this->test_file_name(),
                         "--ldp_addr_histogram", this->test_file_name(),  "--ldp_addr_stats",
                         this->test_file_name(), "--store_pc_histogram",  this->test_file_name(),
                         "--store_pc_stats",     this->test_file_name(),  "--store_addr_histogram",
                         this->test_file_name(), "--store_addr_stats",    this->test_file_name()};
    ampere::tools::memory_tools::MemAnalyzer memAnalyzer(27, arg);

    // window 1
    ampere::is::Instruction instruction;
    instruction.mutable_program_counter()->set_va(0x11111111);
    instruction.set_opcode(0b00111100000000000000010000000000); // STR
    instruction.add_memop()->set_va(0xaaaaaaaa);
    memAnalyzer.ProcessInstr(instruction); // 1 instruction
    ASSERT_EQ(1, memAnalyzer.instruction_count);
    ASSERT_EQ(0, memAnalyzer.ld_pc_count);
    ASSERT_EQ(0, memAnalyzer.ldp_pc_count);
    ASSERT_EQ(1, memAnalyzer.st_pc_count);
    ASSERT_EQ(1, memAnalyzer.get_window_st_pc_count());
    instruction.mutable_program_counter()->set_va(0x22222222);
    instruction.mutable_memop(0)->set_va(0xbbbbbbbb);
    memAnalyzer.ProcessInstr(instruction); // 2 instructions
    ASSERT_EQ(2, memAnalyzer.instruction_count);
    ASSERT_EQ(0, memAnalyzer.ld_pc_count);
    ASSERT_EQ(0, memAnalyzer.ldp_pc_count);
    ASSERT_EQ(2, memAnalyzer.st_pc_count);
    ASSERT_EQ(0, memAnalyzer.get_window_st_pc_count());

    // window 2
    instruction.mutable_program_counter()->set_va(0x33333333);
    instruction.set_opcode(0b00011000000000000000000000000000); // LDR
    instruction.mutable_memop(0)->set_va(0xcccccccc);
    memAnalyzer.ProcessInstr(instruction); // 3 instructions
    ASSERT_EQ(3, memAnalyzer.instruction_count);
    ASSERT_EQ(1, memAnalyzer.ld_pc_count);
    ASSERT_EQ(0, memAnalyzer.ldp_pc_count);
    ASSERT_EQ(2, memAnalyzer.st_pc_count);
    ASSERT_EQ(1, memAnalyzer.get_window_ld_pc_count());
    instruction.mutable_program_counter()->set_va(0x44444444);
    instruction.mutable_memop(0)->set_va(0xdddddddd);
    memAnalyzer.ProcessInstr(instruction); // 4 instructions
    ASSERT_EQ(4, memAnalyzer.instruction_count);
    ASSERT_EQ(2, memAnalyzer.ld_pc_count);
    ASSERT_EQ(0, memAnalyzer.ldp_pc_count);
    ASSERT_EQ(2, memAnalyzer.st_pc_count);
    ASSERT_EQ(0, memAnalyzer.get_window_ld_pc_count());

    // window 3
    instruction.mutable_program_counter()->set_va(0x55555555);
    instruction.set_opcode(0b00101000110000000000000000000000); // LDP
    instruction.mutable_memop(0)->set_va(0xeeeeeeee);
    instruction.add_memop()->set_va(0xffffffff);
    memAnalyzer.ProcessInstr(instruction); // 5 instructions
    ASSERT_EQ(5, memAnalyzer.instruction_count);
    ASSERT_EQ(4, memAnalyzer.ld_pc_count);
    ASSERT_EQ(1, memAnalyzer.ldp_pc_count);
    ASSERT_EQ(2, memAnalyzer.st_pc_count);
    ASSERT_EQ(1, memAnalyzer.get_window_ldp_pc_count());
    instruction.mutable_program_counter()->set_va(0x66666666);
    instruction.set_opcode(0b00011000000000000000000000000000); // LDR
    instruction.clear_memop();
    instruction.add_memop()->set_va(0xfefefefe);
    memAnalyzer.ProcessInstr(instruction); // 6 instructions
    ASSERT_EQ(6, memAnalyzer.instruction_count);
    ASSERT_EQ(5, memAnalyzer.ld_pc_count);
    ASSERT_EQ(1, memAnalyzer.ldp_pc_count);
    ASSERT_EQ(2, memAnalyzer.st_pc_count);
    ASSERT_EQ(0, memAnalyzer.get_window_ldp_pc_count());

    // LD PC globals
    ASSERT_EQ(1, memAnalyzer.get_global_ld_pc_counts()[0]); // 1 window w/ 0 LDs
    ASSERT_EQ(0, memAnalyzer.get_global_ld_pc_counts()[1]); // 0 windows w/ 1 LD
    ASSERT_EQ(1, memAnalyzer.get_global_ld_pc_counts()[2]); // 1 window w/ 2 LDs
    ASSERT_EQ(1, memAnalyzer.get_global_ld_pc_counts()[3]); // 1 window w/ 3 LDs (due to LDP)

    // ST PC globals
    ASSERT_EQ(2, memAnalyzer.get_global_st_pc_counts()[0]); // 1 window w/ 0 STs
    ASSERT_EQ(0, memAnalyzer.get_global_st_pc_counts()[1]); // 0 windows w/ 1 ST
    ASSERT_EQ(1, memAnalyzer.get_global_st_pc_counts()[2]); // 1 window w/ 2 STs

    // LDP PC globals
    ASSERT_EQ(2, memAnalyzer.get_global_ldp_pc_counts()[0]); // 2 window w/ 0 LDPs
    ASSERT_EQ(1, memAnalyzer.get_global_ldp_pc_counts()[1]); // 1 windows w/ 1 LPD
    ASSERT_EQ(0, memAnalyzer.get_global_ldp_pc_counts()[2]); // 0 window w/ 2 LDPs

    // LD PC map
    ASSERT_EQ(4, memAnalyzer.get_total_ld_pc_map().size());
    ASSERT_EQ(0, memAnalyzer.get_total_ld_pc_map()[0x33333333][0]); // 0 window w/ 1 concurrent LD PC
    ASSERT_EQ(1, memAnalyzer.get_total_ld_pc_map()[0x33333333][1]); // 1 window w/ 2 concurrent LD PCs
    ASSERT_EQ(0, memAnalyzer.get_total_ld_pc_map()[0x44444444][0]); // 0 window w/ 1 concurrent LD PC
    ASSERT_EQ(1, memAnalyzer.get_total_ld_pc_map()[0x44444444][1]); // 1 window w/ 2 concurrent LD PCs
    ASSERT_EQ(0, memAnalyzer.get_total_ld_pc_map()[0x55555555][0]); // 0 window w/ 1 concurrent LD PC
    ASSERT_EQ(0, memAnalyzer.get_total_ld_pc_map()[0x55555555][1]); // 0 window w/ 2 concurrent LD PCs
    ASSERT_EQ(1, memAnalyzer.get_total_ld_pc_map()[0x55555555][2]); // 1 window w/ 3 concurrent LD PCs
    ASSERT_EQ(0, memAnalyzer.get_total_ld_pc_map()[0x66666666][0]); // 0 window w/ 1 concurrent LD PC
    ASSERT_EQ(0, memAnalyzer.get_total_ld_pc_map()[0x66666666][1]); // 0 window w/ 2 concurrent LD PCs
    ASSERT_EQ(1, memAnalyzer.get_total_ld_pc_map()[0x66666666][2]); // 1 window w/ 3 concurrent LD PCs

    // ST PC map
    ASSERT_EQ(2, memAnalyzer.get_total_st_pc_map().size());
    ASSERT_EQ(0, memAnalyzer.get_total_st_pc_map()[0x11111111][0]); // 0 window w/ 1 concurrent ST PC
    ASSERT_EQ(1, memAnalyzer.get_total_st_pc_map()[0x11111111][1]); // 1 window w/ 2 concurrent ST PCs
    ASSERT_EQ(0, memAnalyzer.get_total_st_pc_map()[0x22222222][0]); // 0 window w/ 1 concurrent ST PC
    ASSERT_EQ(1, memAnalyzer.get_total_st_pc_map()[0x22222222][1]); // 1 window w/ 2 concurrent ST PCs

    // LDP PC map
    ASSERT_EQ(1, memAnalyzer.get_total_ldp_pc_map().size());
    ASSERT_EQ(1, memAnalyzer.get_total_ldp_pc_map()[0x55555555][0]); // 1 window w/ 1 concurrent ST PC

    // LD ADDR globals
    ASSERT_EQ(1, memAnalyzer.get_global_ld_addr_counts()[0]); // 1 window w/ 0 LDs
    ASSERT_EQ(0, memAnalyzer.get_global_ld_addr_counts()[1]); // 0 windows w/ 1 LD
    ASSERT_EQ(1, memAnalyzer.get_global_ld_addr_counts()[2]); // 1 window w/ 2 LDs
    ASSERT_EQ(1, memAnalyzer.get_global_ld_addr_counts()[3]); // 1 window w/ 3 LDs

    // ST ADDR globals
    ASSERT_EQ(2, memAnalyzer.get_global_st_addr_counts()[0]); // 2 window w/ 0 STs
    ASSERT_EQ(0, memAnalyzer.get_global_st_addr_counts()[1]); // 0 windows w/ 1 ST
    ASSERT_EQ(1, memAnalyzer.get_global_st_addr_counts()[2]); // 1 window w/ 2 STs

    // LDP ADDR globals
    ASSERT_EQ(2, memAnalyzer.get_global_ldp_addr_counts()[0]); // 2 window w/ 0 LDPs
    ASSERT_EQ(0, memAnalyzer.get_global_ldp_addr_counts()[1]); // 0 windows w/ 1 LPD
    ASSERT_EQ(1, memAnalyzer.get_global_ldp_addr_counts()[2]); // 1 window w/ 2 LDPs

    // LD ADDR map
    ASSERT_EQ(5, memAnalyzer.get_total_ld_addr_map().size());
    ASSERT_EQ(0, memAnalyzer.get_total_ld_addr_map()[0xcccccccc][0]); // 0 window w/ 1 concurrent LD addr
    ASSERT_EQ(1, memAnalyzer.get_total_ld_addr_map()[0xcccccccc][1]); // 1 window w/ 2 concurrent LD addrs
    ASSERT_EQ(0, memAnalyzer.get_total_ld_addr_map()[0xdddddddd][0]); // 0 window w/ 1 concurrent LD addr
    ASSERT_EQ(1, memAnalyzer.get_total_ld_addr_map()[0xdddddddd][1]); // 1 window w/ 2 concurrent LD addrs
    ASSERT_EQ(0, memAnalyzer.get_total_ld_addr_map()[0xeeeeeeee][0]); // 0 window w/ 1 concurrent LD addr
    ASSERT_EQ(0, memAnalyzer.get_total_ld_addr_map()[0xeeeeeeee][1]); // 0 window w/ 2 concurrent LD addrs
    ASSERT_EQ(1, memAnalyzer.get_total_ld_addr_map()[0xeeeeeeee][2]); // 1 window w/ 3 concurrent LD addrs
    ASSERT_EQ(0, memAnalyzer.get_total_ld_addr_map()[0xffffffff][0]); // 0 window w/ 1 concurrent LD addr
    ASSERT_EQ(0, memAnalyzer.get_total_ld_addr_map()[0xffffffff][1]); // 0 window w/ 2 concurrent LD addrs
    ASSERT_EQ(1, memAnalyzer.get_total_ld_addr_map()[0xffffffff][2]); // 1 window w/ 3 concurrent LD addrs
    ASSERT_EQ(0, memAnalyzer.get_total_ld_addr_map()[0xfefefefe][0]); // 0 window w/ 1 concurrent LD addr
    ASSERT_EQ(0, memAnalyzer.get_total_ld_addr_map()[0xfefefefe][1]); // 0 window w/ 2 concurrent LD addrs
    ASSERT_EQ(1, memAnalyzer.get_total_ld_addr_map()[0xfefefefe][2]); // 1 window w/ 3 concurrent LD addrs

    // ST ADDR map
    ASSERT_EQ(2, memAnalyzer.get_total_st_addr_map().size());
    ASSERT_EQ(0, memAnalyzer.get_total_st_addr_map()[0xaaaaaaaa][0]); // 0 window w/ 1 concurrent ST addr
    ASSERT_EQ(1, memAnalyzer.get_total_st_addr_map()[0xaaaaaaaa][1]); // 1 window w/ 2 concurrent ST addrs
    ASSERT_EQ(0, memAnalyzer.get_total_st_addr_map()[0xbbbbbbbb][0]); // 0 window w/ 1 concurrent ST addr
    ASSERT_EQ(1, memAnalyzer.get_total_st_addr_map()[0xbbbbbbbb][1]); // 1 window w/ 2 concurrent ST addrs

    // LDP ADDR map
    ASSERT_EQ(2, memAnalyzer.get_total_ldp_addr_map().size());
    ASSERT_EQ(0, memAnalyzer.get_total_ldp_addr_map()[0xeeeeeeee][0]); // 0 window w/ 1 concurrent LDP addr
    ASSERT_EQ(1, memAnalyzer.get_total_ldp_addr_map()[0xeeeeeeee][1]); // 1 window w/ 2 concurrent LDP addrs
    ASSERT_EQ(0, memAnalyzer.get_total_ldp_addr_map()[0xffffffff][0]); // 0 window w/ 1 concurrent LDP addr
    ASSERT_EQ(1, memAnalyzer.get_total_ldp_addr_map()[0xffffffff][1]); // 1 window w/ 2 concurrent LDP addrs

    memAnalyzer.StatsOutput();
}

TEST_F(MemAnalyzerTest, testCalcStdDev)
{
    char const *arg[] = {"mem_analyzer", "--window_size", "2"};
    ampere::tools::memory_tools::MemAnalyzer memAnalyzer(3, arg);

    double stdDev = 0.0;
    double mean = 0.0;
    std::vector<uint64_t> inArr = {1, 2, 3, 4, 5, 6, 7, 8, 9};

    memAnalyzer.CalcStdDev(inArr, stdDev, mean);

    // Set lower precision for stdDev
    stdDev = floor(stdDev * 100000) / 100000;

    ASSERT_EQ(5, mean);
    ASSERT_EQ(2.58198, stdDev);
}

} // namespace tools
} // namespace ampere

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
