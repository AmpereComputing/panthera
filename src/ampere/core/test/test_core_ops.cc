//
// Copyright (c) 2018-2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include <boost/algorithm/string/predicate.hpp>

#include "ampere/core/test/core_integration.h"

namespace bfs = boost::filesystem;

using namespace ::testing;

TEST_F(ConnorCoreIntegrationTest, testDependentAdds)
{
    run_instruction_stream(TestModule::testfixture("dependent_adds_chop.is.xz"), 5000, 50000_ns);

    EXPECT_NEAR(simulated_ipc(), 1.0, 0.1);
}

TEST_F(ConnorCoreIntegrationTest, testIndependentAdds)
{
    run_instruction_stream(TestModule::testfixture("independent_adds_chop.is.xz"), 5000, 20000_ns);

    EXPECT_NEAR(simulated_ipc(), 4.0, 0.2);
}

TEST_F(ConnorCoreIntegrationTest, testDependentMuls)
{
    run_instruction_stream(TestModule::testfixture("dependent_muls_chop.is.xz"), 1000, 50000_ns);

    EXPECT_NEAR(simulated_ipc(), 0.25, 0.1);
}

TEST_F(ConnorCoreIntegrationTest, testIndependentMuls)
{
    run_instruction_stream(TestModule::testfixture("independent_muls_chop.is.xz"), 1000, 20000_ns);

    EXPECT_NEAR(simulated_ipc(), 1.0, 0.2);
}

TEST_F(RipleyCoreIntegrationTest, testDependentFmuls)
{
    run_instruction_stream(TestModule::testfixture("dependent_fmuls_chop.is.xz"), 1000, 50000_ns);

    EXPECT_NEAR(simulated_ipc(), 0.1875, 0.01);
}

TEST_F(ConnorCoreIntegrationTest, testDependentFmulsConnor)
{
    run_instruction_stream(TestModule::testfixture("dependent_fmuls_chop.is.xz"), 1000, 50000_ns);

    EXPECT_NEAR(simulated_ipc(), 0.281, 0.01);
}

TEST_F(ConnorCoreIntegrationTest, testIndependentFmuls)
{
    run_instruction_stream(TestModule::testfixture("independent_fmuls_chop.is.xz"), 1000, 20000_ns);

    EXPECT_NEAR(simulated_ipc(), 2.25, 0.2);
}

TEST_F(RipleyCoreIntegrationTest, testIndependentFaddsStores)
{
    run_instruction_stream(TestModule::testfixture("independent_fadds_stores_chop.is.xz"), 2000, 40000_ns);

    EXPECT_NEAR(simulated_ipc(), 3.1, 0.15);
}

TEST_F(TrinityCoreIntegrationTest, testIndependentFaddsStoresTrinity)
{
    run_instruction_stream(TestModule::testfixture("independent_fadds_stores_chop.is.xz"), 2000, 40000_ns);

    // Trinity reduced FSU scheduler from 8x3 to 18x1, which hurts performance
    // on this test because of scheduler deallocation bandwidth
    EXPECT_NEAR(simulated_ipc(), 2.17, 0.15);
}

TEST_F(ConnorCoreIntegrationTest, testIndependentFaddsStoresConnor)
{
    run_instruction_stream(TestModule::testfixture("independent_fadds_stores_chop.is.xz"), 2000, 40000_ns);

    // Connor improved scheduler deallocation bandwith to 2/cycle
    EXPECT_NEAR(simulated_ipc(), 3.25, 0.1);
}

TEST_F(RipleyCoreIntegrationTest, testDependentLoads)
{
    run_instruction_stream(TestModule::testfixture("dependent_loads_chop.is.xz"), 1000, 50000_ns);

    EXPECT_NEAR(simulated_ipc(), 0.25, 0.02);
}

TEST_F(TrinityCoreIntegrationTest, testDependentLoadsTrinity)
{
    run_instruction_stream(TestModule::testfixture("dependent_loads_chop.is.xz"), 1000, 50000_ns);

    EXPECT_NEAR(simulated_ipc(), 0.336, 0.02);
}

TEST_F(ConnorCoreIntegrationTest, testDependentLoadsConnor)
{
    run_instruction_stream(TestModule::testfixture("dependent_loads_chop.is.xz"), 1000, 50000_ns);

    EXPECT_NEAR(simulated_ipc(), 0.25, 0.02);
}

TEST_F(ConnorCoreIntegrationTest, testIndependentLoads)
{
    run_instruction_stream(TestModule::testfixture("independent_loads_chop.is.xz"), 1000, 20000_ns);

    EXPECT_NEAR(simulated_ipc(), 2.5, 0.2);
}

TEST_F(ConnorCoreIntegrationTest, testIndependentStores)
{
    run_instruction_stream(TestModule::testfixture("independent_stores_chop.is.xz"), 1000, 20000_ns);

    // IPC is 1.25 without store combing enabled via
    // `-sys.soc_0.core_0.lsu.combine_stores 1`
    EXPECT_NEAR(simulated_ipc(), 2.5, 0.05);
}

TEST_F(ConnorCoreIntegrationTest, testLoadStoreNoStoreCombining)
{
    get_environment()->parameters.add_setting("sys.soc_0.core_0.lsu.combine_stores", "0", "test bench");

    run_instruction_stream(TestModule::testfixture("load_store_chop.is.xz"), 1000, 20000_ns);

    EXPECT_NEAR(simulated_ipc(), 2.25, 0.05);
}

TEST_F(ConnorCoreIntegrationTest, testLoadStoreCombining)
{
    run_instruction_stream(TestModule::testfixture("load_store_chop.is.xz"), 1000, 20000_ns);

    EXPECT_NEAR(simulated_ipc(), 4.25, 0.05);
}

TEST_F(ConnorCoreIntegrationTest, testIndependentAddsLoads)
{
    run_instruction_stream(TestModule::testfixture("independent_adds_loads_chop.is.xz"), 1000, 20000_ns);

    EXPECT_NEAR(simulated_ipc(), 4.5, 0.2);
}

TEST_F(ConnorCoreIntegrationTest, testMovMovkFusion)
{
    run_instruction_stream(TestModule::testfixture("mov_movk_fusion_chop.is.xz"), 1000, 20000_ns);

    EXPECT_NEAR(simulated_ipc(), 5.0, 0.25);
}

// Currently, we don't fuse 3 uops (movz, movk, movk)  Also this ubenchmark also
// tests to make sure we don't fuse reverse shift order
TEST_F(BansheeCoreIntegrationTest, testMovMovkMovkFusion)
{
    run_instruction_stream(TestModule::testfixture("mov_movk_movk_fusion_chop.is.xz"), 1000, 20000_ns);

    EXPECT_NEAR(simulated_ipc(), 4.0, 0.25);
}

TEST_F(RogueCoreIntegrationTest, testMovzMovkFusionTrinity)
{
    run_instruction_stream(TestModule::testfixture("movz_movk_fusion_trinity.is.xz"), 1000, 20000_ns);

    EXPECT_NEAR(simulated_ipc(), 4.75, 0.25);
}

TEST_F(RogueCoreIntegrationTest, testMovkMovkFusionTrinity)
{
    run_instruction_stream(TestModule::testfixture("movk_movk_fusion_trinity.is.xz"), 1000, 20000_ns);

    EXPECT_NEAR(simulated_ipc(), 4.67, 0.25);
}

TEST_F(ConnorCoreIntegrationTest, testDoubleBranchLoop)
{
    get_environment()->parameters.add_setting("sys.soc_0.core_0.bpu.commits_per_cycle", 2, "test bench");

    run_instruction_stream(TestModule::testfixture("double_branch_loop_chop.is.xz"), 10000, 20000_ns);

    EXPECT_NEAR(simulated_ipc(), 3, 0.1);
}

TEST_F(ConnorCoreIntegrationTest, testTLBInvalidations)
{
    run_instruction_stream(TestModule::testfixture("502.gcc-2.870_0_chopped.is.xz"), 10000, 30000_ns);

    EXPECT_EQ(event_count("sys.soc_0.core_0.dmmu.tlb_invalidate"), 1);
}

TEST_F(ConnorCoreIntegrationTest, RapidCallReturn)
{
    // get_environment()->event_stream = std::make_unique<event_stream::EventStreamProto>("rapid_call_ret.es");
    run_instruction_stream(TestModule::testfixture("rapid_call_ret.is.xz"), 10000, 20000_ns);

    // NOP fusion brought IPC down to 1.55 from 1.75
    EXPECT_NEAR(simulated_ipc(), 2.00, 0.05);
}

TEST_F(CoreIntegrationTest, testLoadCancelIXU)
{
    get_environment()->parameters.load(TestModule::testconfig("sys/domino.cfg"));
    get_environment()->parameters.add_setting("sys.soc_0.core_0.lsu.splits_send_cancel", true, "test bench");

    run_instruction_stream(TestModule::testfixture("load_cancel_ixu.is.xz"), 10000, 20000_ns);

    // There should be lots of loads, and except for a little startup
    // they should all cancel once and wake two dependents
    EXPECT_GE(event_count("sys.soc_0.core_0.lsu.uop_ld_complete"), 1000);
    EXPECT_NEAR(
        event_count("sys.soc_0.core_0.lsu.uop_ld_complete"), event_count("sys.soc_0.core_0.ixu.load_cancel"), 20);
    EXPECT_NEAR(
        event_count("sys.soc_0.core_0.ixu.uop_cancel"), event_count("sys.soc_0.core_0.ixu.load_cancel") * 2, 75);
}

TEST_F(CoreIntegrationTest, testLoadCancelFSU)
{
    get_environment()->parameters.load(TestModule::testconfig("sys/domino.cfg"));
    get_environment()->parameters.add_setting("sys.soc_0.core_0.lsu.splits_send_cancel", true, "test bench");

    run_instruction_stream(TestModule::testfixture("load_cancel_fsu.is.xz"), 10000, 20000_ns);

    // There should be lots of loads, and except for a little startup
    // they should all cancel once and wake two dependents
    EXPECT_GE(event_count("sys.soc_0.core_0.lsu.uop_ld_complete"), 1000);
    EXPECT_NEAR(
        event_count("sys.soc_0.core_0.lsu.uop_ld_complete"), event_count("sys.soc_0.core_0.ixu.load_cancel"), 20);
    EXPECT_NEAR(
        event_count("sys.soc_0.core_0.ixu.uop_cancel"), event_count("sys.soc_0.core_0.ixu.load_cancel") * 2, 75);
}

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
