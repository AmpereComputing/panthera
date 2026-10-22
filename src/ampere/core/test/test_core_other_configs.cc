//
// Copyright (c) 2018-2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include <gtest/gtest.h>
#include <boost/algorithm/string/predicate.hpp>

#include "ampere/core/test/core_integration.h"

namespace bfs = boost::filesystem;

using namespace ::testing;

TEST_F(ConnorCoreIntegrationTest, testDhrystone)
{
    run_instruction_stream(TestModule::testfixture("dhrystone.is.xz"), 500, 20000_ns);

    EXPECT_GE(event_count("sys.soc_0.core_0.gpc.uop_commit"), 486);
    EXPECT_NEAR(event_count("sys.soc_0.core_0.gpc.instruction_commit"), 502, 2);
}

TEST_F(ConnorCoreIntegrationTest, test6wide4FSU)
{
    get_environment()->parameters.add_setting(
        "sys.soc_0.core_0:append-cfg", TestModule::testconfig("core/wider/6-wide.cfg"), "unit test");
    get_environment()->parameters.add_setting("sys.soc_0.core_0.idr.fsu_partitions", 2, "test bench");

    run_instruction_stream(TestModule::testfixture("independent_fadds_stores_chop.is.xz"), 5000, 20000_ns);

    EXPECT_NEAR(simulated_ipc(), 6.00, 0.1);
}

TEST_F(ConnorCoreIntegrationTest, test8wide8int)
{
    get_environment()->parameters.add_setting(
        "sys.soc_0.core_0:append-cfg", TestModule::testconfig("core/wider/8-wide.cfg"), "unit test");
    get_environment()->parameters.add_setting("sys.soc_0.core_0:append-cfg",
                                              TestModule::testconfig("core/alita/pathfinding/more-exec-base.cfg"),
                                              "unit test");
    get_environment()->parameters.add_setting("sys.soc_0.core_0.idr.a_uops_on_j_pipe", true, "test bench");
    get_environment()->parameters.add_setting("sys.soc_0.core_0.idr.b_uops_on_k_pipe", true, "test bench");

    run_instruction_stream(TestModule::testfixture("independent_adds_chop.is.xz"), 5000, 20000_ns);

    EXPECT_NEAR(simulated_ipc(), 8.00, 0.1);
}

TEST_F(ConnorCoreIntegrationTest, test4loads)
{
    get_environment()->parameters.add_setting("sys.soc_0.core_0:append-cfg",
                                              TestModule::testconfig("core/alita/pathfinding/four-mem-pipes.cfg"),
                                              "unit test");

    run_instruction_stream(TestModule::testfixture("independent_loads_chop.is.xz"), 5000, 20000_ns);

    EXPECT_NEAR(simulated_ipc(), 4.4, 0.1);
}

TEST_F(BansheeCoreIntegrationTest, testBanshee_PerfectDL1)
{
    get_environment()->parameters.add_setting("sys.soc_0.core_0.lsu.l1d_cache.enable_perfect_mode", true, "test bench");

    run_instruction_stream(TestModule::testfixture("lat_mem_rd_128M.is_0.is.xz"), 200000_ns);

    EXPECT_LE(event_count("sys.soc_0.core_0.gpc.cycle"), 800);
}

TEST_F(CoreIntegrationTest, testRtlRogue)
{
    get_environment()->parameters.load(TestModule::testconfig("sys/rogue-core-rtl.cfg"));

    run_instruction_stream(TestModule::testfixture("java11-dacapo-sunflow_97_20200601_4dbec4.is.xz"),
                           1000,
                           10000_ns); // stream doesn't matter
}

// This is mostly a test that the retired_mispredicted_branches() is working as
// intended, so that it can be relied upon to test that the perfect frontends
// do not have any mispredicted branches
TEST_F(ConnorCoreIntegrationTest, testNormalFrontend)
{
    run_instruction_stream(TestModule::testfixture("502.gcc-2.870_0_chopped.is.xz"), 1000, 20000_ns);

    // Make sure we're recording at least a handful of mispredicted branches
    EXPECT_GE(retired_mispredicted_branches(), 10);
}

TEST_F(ConnorCoreIntegrationTest, testAlmostPerfectFrontend)
{
    get_environment()->parameters.load(TestModule::testconfig("core/furiosa/almost-perfect-frontend.cfg"),
                                       "sys.soc_0.core_0");

    run_instruction_stream(TestModule::testfixture("cputest_btbcapacity0_0_chopped.is.xz"), 10000, 20000_ns);

    EXPECT_NEAR(simulated_ipc(), 1.5, 0.01);
    EXPECT_EQ(retired_mispredicted_branches(), 0);
}

TEST_F(CoreIntegrationTest, testPerfectFrontend)
{
    get_environment()->parameters.load(TestModule::testconfig("sys/domino-perfect-frontend.cfg"));

    run_instruction_stream(TestModule::testfixture("cputest_btbcapacity0_0_chopped.is.xz"), 10000, 20000_ns);

    // The most the 'real' machine can do for this trace is 1.5 IPC, since it
    // features a 3 instruction loop with two branches, but can only fetch one
    // taken branch per cycle. We can beat that since we have no such
    // restriction.
    EXPECT_NEAR(simulated_ipc(), 3, 0.1);
    EXPECT_EQ(retired_mispredicted_branches(), 0);
}

TEST_F(CoreIntegrationTest, testPerfectFrontend2)
{
    get_environment()->parameters.add_setting("sys.soc_0.core_0.pfe.dump_at_end", "true", "test bench");
    get_environment()->parameters.load(TestModule::testconfig("sys/domino-perfect-frontend.cfg"));

    run_instruction_stream(TestModule::testfixture("502.gcc-2.870_0_chopped.is.xz"), 10000, 20000_ns);

    EXPECT_EQ(retired_mispredicted_branches(), 0);
}

TEST_F(RogueCoreIntegrationTest, testMTE)
{
    get_environment()->parameters.add_setting("sys.soc_0.core_0.lsu.enable_mte", "true", "test bench");
    get_environment()->parameters.add_setting("sys.soc_0.core_0.l2c.disable_make_unique", "true", "test bench");

    run_instruction_stream(TestModule::testfixture("ren-scrabble-jdk11.256_34_20220425_0a2c15.is.xz"),
                           200000,
                           25000_ns); // about 12K instructions

    uint64_t sta_load_complete = event_count("sys.soc_0.core_0.lsu.uop_sta_ld_complete");
    uint64_t sta_complete = event_count("sys.soc_0.core_0.lsu.uop_sta_complete");

    // STAs to STA-loads are around 2:1
    EXPECT_NEAR(static_cast<float>(sta_complete) / sta_load_complete, 2, 0.23);
}

struct LSUTagReadPortsWithMTETest : CoreIntegrationTest
{
    inline static const uint64_t RUN_INSTRUCTIONS = 200000;

    void SetUp() override
    {
        get_environment()->parameters.load(TestModule::testconfig("sys/domino.cfg"));
        get_environment()->parameters.add_setting("sys.soc_0.core_0.lsu.enable_mte", "true", "test bench");
        get_environment()->parameters.add_setting("sys.soc_0.core_0.l2c.disable_make_unique", "true", "test bench");
        // Disable some skips as they get the trace into a bad beat pattern with conflicts
        get_environment()->parameters.add_setting("sys.soc_0.core_0.lsu.sus_skip_final_sta", "false", "test bench");
        get_environment()->parameters.add_setting("sys.soc_0.core_0.lsu.dmb_all_skip_final_sta", "false", "test bench");
        get_environment()->parameters.add_setting("sys.soc_0.core_0.lsu.dsb_skip_final_sta", "false", "test bench");
        get_environment()->parameters.add_setting(
            "sys.soc_0.core_0.lsu.instantly_complete_uops_not_needing_sta", "false", "test bench");
        // The full-line write into the IQ also causes the bad beat pattern with conflicts
        get_environment()->parameters.add_setting("sys.soc_0.core_0.icf.half_cacheline_fetch", "true", "test bench");
    }
};

// Disabled for now because this trace is too flaky -- many unrelated changes push it into a beat pattern which causes
// it to fail
TEST_F(LSUTagReadPortsWithMTETest, DISABLED_testNoPrefetching)
{
    get_environment()->parameters.add_setting("sys.soc_0.core_0.lsu.store_pipes_have_tag_ports", "true", "test bench");

    run_instruction_stream(TestModule::testfixture("load_store_chop.is.xz"), RUN_INSTRUCTIONS, 10000_ns);

    uint64_t cycles = event_count("sys.soc_0.core_0.gpc.cycle");
    uint64_t load_complete = event_count("sys.soc_0.core_0.lsu.uop_ld_complete");
    uint64_t sta_load_complete = event_count("sys.soc_0.core_0.lsu.uop_sta_ld_complete");

    EXPECT_NEAR((load_complete + sta_load_complete) / cycles, 4, 1);
}

TEST_F(LSUTagReadPortsWithMTETest, DISABLED_testPrefetching)
{
    // This used to specify 4 tag read pipes, which can't be done now that they are tied to store pipes
    get_environment()->parameters.add_setting("sys.soc_0.core_0.lsu.store_pipes_have_tag_ports", "true", "test bench");

    run_instruction_stream(TestModule::testfixture("505.mcf-1.59701_chop.is.xz"), RUN_INSTRUCTIONS, 40000_ns);

    uint64_t pcp_launch = event_count("sys.soc_0.core_0.lsu.prefetcher.pcp.launch");
    uint64_t sta_load_complete = event_count("sys.soc_0.core_0.lsu.uop_sta_ld_complete");

    EXPECT_GE(sta_load_complete, 128);
    EXPECT_GE(pcp_launch, 120);
}

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
