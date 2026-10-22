//
// Copyright (c) 2018-2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include <boost/algorithm/string/predicate.hpp>

#include "ampere/core/test/core_integration.h"

namespace bfs = boost::filesystem;

using namespace ::testing;

TEST_F(FuriosaCoreIntegrationTest, LoadLatencyL1)
{
    run_instruction_stream(TestModule::testfixture("lat_mem_rd_32K.is_0.is.xz"), 500, 100000_ns);

    uint64_t ld_complete = event_count("sys.soc_0.core_0.lsu.uop_ld_complete");
    uint64_t cycles = event_count("sys.soc_0.core_0.gpc.cycle");
    double cycles_per_load = (double)cycles / ld_complete;

    EXPECT_NEAR(cycles_per_load, 4, 0.01);
}

TEST_F(TrinityCoreIntegrationTest, LoadLatencyL1Connor)
{
    run_instruction_stream(TestModule::testfixture("lat_mem_rd_32K.is_0.is.xz"), 500, 100000_ns);

    uint64_t ld_complete = event_count("sys.soc_0.core_0.lsu.uop_ld_complete");
    uint64_t cycles = event_count("sys.soc_0.core_0.gpc.cycle");
    double cycles_per_load = (double)cycles / ld_complete;

    EXPECT_NEAR(cycles_per_load, 3, 0.01);
}

TEST_F(ConnorCoreIntegrationTest, LoadLatencyL1Connor)
{
    run_instruction_stream(TestModule::testfixture("lat_mem_rd_32K.is_0.is.xz"), 500, 100000_ns);

    uint64_t ld_complete = event_count("sys.soc_0.core_0.lsu.uop_ld_complete");
    uint64_t cycles = event_count("sys.soc_0.core_0.gpc.cycle");
    double cycles_per_load = (double)cycles / ld_complete;

    EXPECT_NEAR(cycles_per_load, 4, 0.01);
}

TEST_F(AlitaCoreIntegrationTest, LoadLatencyL1Alita)
{
    run_instruction_stream(TestModule::testfixture("lat_mem_rd_32K.is_0.is.xz"), 400, 100000_ns);

    uint64_t ld_complete = event_count("sys.soc_0.core_0.lsu.uop_ld_complete");
    uint64_t cycles = event_count("sys.soc_0.core_0.gpc.cycle");
    double cycles_per_load = (double)cycles / ld_complete;

    EXPECT_NEAR(cycles_per_load, 4, 0.01);
}

TEST_F(BansheeCoreIntegrationTest, LoadLatencyL2)
{
    run_instruction_stream(TestModule::testfixture("lat_mem_rd_128K.is_0.is.xz"), 1500, 1000000_ns);

    uint64_t ld_complete = event_count("sys.soc_0.core_0.lsu.uop_ld_complete");
    uint64_t cycles = event_count("sys.soc_0.core_0.gpc.cycle");
    double cycles_per_load = (double)cycles / ld_complete;

    EXPECT_NEAR(cycles_per_load, 11, 0.01);
}

TEST_F(DominoCoreIntegrationTest, LoadLatencyL2Connor)
{
    run_instruction_stream(TestModule::testfixture("lat_mem_rd_128K.is_0.is.xz"), 1500, 1000000_ns);

    uint64_t ld_complete = event_count("sys.soc_0.core_0.lsu.uop_ld_complete");
    uint64_t cycles = event_count("sys.soc_0.core_0.gpc.cycle");
    double cycles_per_load = (double)cycles / ld_complete;

    EXPECT_NEAR(cycles_per_load, 13, 0.01);
}

TEST_F(NebulaCoreIntegrationTest, LoadLatencyL2Nebula)
{
    run_instruction_stream(TestModule::testfixture("lat_mem_rd_128K.is_0.is.xz"), 1500, 1000000_ns);

    uint64_t ld_complete = event_count("sys.soc_0.core_0.lsu.uop_ld_complete");
    uint64_t cycles = event_count("sys.soc_0.core_0.gpc.cycle");
    double cycles_per_load = (double)cycles / ld_complete;

    EXPECT_NEAR(cycles_per_load, 15, 0.01);
}

TEST_F(NebulaRateNCoreIntegrationTest, LoadLatencyL2Nebula)
{
    run_instruction_stream(TestModule::testfixture("lat_mem_rd_128K.is_0.is.xz"), 1500, 1000000_ns);

    uint64_t ld_complete = event_count("sys.soc_0.core_0.lsu.uop_ld_complete");
    uint64_t cycles = event_count("sys.soc_0.core_0.gpc.cycle");
    double cycles_per_load = (double)cycles / ld_complete;

    EXPECT_NEAR(cycles_per_load, 13, 0.01);
}

TEST_F(CoreIntegrationTest, testMemoryLatency)
{
    get_environment()->parameters.load(TestModule::testconfig("sys/siryn.cfg"));

    run_instruction_stream(TestModule::testfixture("lat_mem_rd_128M.is_0.is.xz"), 200000_ns);

    EXPECT_NEAR(event_count("sys.soc_0.core_0.gpc.cycle"), 33946, 3000);
}

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
