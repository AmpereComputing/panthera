//
// Copyright (c) 2018-2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include <boost/algorithm/string/predicate.hpp>
#include <gmock/gmock.h>

#include "ampere/core/test/core_integration.h"
#include "ampere/util/testing/capture.h"

namespace bfs = boost::filesystem;

using namespace ::testing;

TEST_F(ConnorCoreIntegrationTest, MaxInstructions)
{
    run_instruction_stream(TestModule::testfixture("dhrystone.is.xz"), 500, 20000_ns);

    EXPECT_GE(event_count("sys.soc_0.core_0.gpc.uop_commit"), 486);
    EXPECT_NEAR(event_count("sys.soc_0.core_0.gpc.instruction_commit"), 502, 2);
}

TEST_F(ConnorCoreIntegrationTest, MaxWarmupInstructions)
{
    auto capture = CaptureStdout();

    get_environment()->parameters.add_setting("sys.soc_0.core_0.gpc.max_warmup_instructions", 100, "test bench");
    run_instruction_stream(TestModule::testfixture("dhrystone.is.xz"), 500, 20000_ns);

    EXPECT_THAT(*capture, HasSubstr("100 instructions total"));
    EXPECT_EQ(event_count("sys.soc_0.core_0.gpc.instruction_commit"), 0);
}

TEST_F(ConnorCoreIntegrationTest, WarmupInstructions)
{
    auto capture = CaptureStdout();

    get_environment()->parameters.add_setting("sys.soc_0.core_0.gpc.warmup_instructions", 100, "test bench");
    run_instruction_stream(TestModule::testfixture("dhrystone.is.xz"), 500, 20000_ns);

    EXPECT_THAT(*capture, ContainsRegex("60[0-2] instructions total"));
    EXPECT_THAT(*capture, ContainsRegex("Simulator warmup .* 100 instructions"));
    EXPECT_NEAR(event_count("sys.soc_0.core_0.gpc.instruction_commit"), 502, 2);
}

TEST_F(ConnorCoreIntegrationTest, NoWarmupInstructions)
{
    auto capture = CaptureStdout();

    get_environment()->parameters.add_setting("sys.soc_0.core_0.gpc.warmup_instructions", 0, "test bench");
    run_instruction_stream(TestModule::testfixture("dhrystone.is.xz"), 500, 20000_ns);

    EXPECT_THAT(*capture, ContainsRegex("50[5-9] instructions total"));
    EXPECT_THAT(*capture, ContainsRegex("Simulator warmup .* 0 instructions"));
    EXPECT_NEAR(event_count("sys.soc_0.core_0.gpc.instruction_commit"), 507, 2);
}

TEST_F(ConnorCoreIntegrationTest, BuildingWrongPathMemoryImage)
{
    get_environment()->parameters.add_setting("sys.soc_0.core_0.speculative_memory_image", "true", "test bench");

    run_instruction_stream(TestModule::testfixture("502.gcc-3.13171_chop.is.xz"), 5000, 30000_ns);

    EXPECT_NEAR(event_count("sys.soc_0.core_0.gpc.instruction_commit"), 5000, 10);
}

TEST_F(BansheeCoreIntegrationTest, L2CWarmstart)
{
    get_environment()->parameters.add_setting("sys.soc_0.warmstart_enable", "false", "test bench");
    get_environment()->parameters.add_setting(
        "sys.soc_0.core_0.l2c.warmup_ms", TestModule::testfixture("java14-dacapo-biojava_3.ms.xz"), "test bench");
    get_environment()->parameters.add_setting("~sys\\.soc_0\\.homenode_[0-9]+\\.enable_slc", "false", "test bench");

    run_instruction_stream(TestModule::testfixture("java14-dacapo-biojava_3.is.xz"), 25000, 25000_ns);

    EXPECT_NEAR(event_count("sys.soc_0.core_0.gpc.instruction_commit"), 25000, 10);
    EXPECT_LE(event_count("sys.soc_0.core_0.l2c.read_pipeline.return"), 7000);
    EXPECT_GE(simulated_ipc(), 0.5);
}

TEST_F(RogueCoreIntegrationTest, SLCWarmstart)
{
    get_environment()->parameters.add_setting(
        "sys.soc_0.warmup_ms", TestModule::testfixture("java14-dacapo-biojava_3.ms.xz"), "test bench");

    run_instruction_stream(TestModule::testfixture("java14-dacapo-biojava_3.is.xz"), 25000, 30000_ns);

    EXPECT_NEAR(event_count("sys.soc_0.core_0.gpc.instruction_commit"), 25000, 10);
    EXPECT_LE(event_count("top.memory.memory_request"), 1450);
    EXPECT_GE(simulated_ipc(), 0.40);
}

TEST_F(ConnorCoreIntegrationTest, UnifiedWarmstart)
{
    auto capture = CaptureStdout();

    get_environment()->parameters.add_setting(
        "sys.soc_0.warmup_ms", TestModule::testfixture("745.brotli-1.3657_0.ms.xz"), "test bench");
    get_environment()->parameters.add_setting("sys.soc_0.unified_warmstart", "true", "test bench");

    // Instruction stream doesn't matter, just testing warmup
    run_instruction_stream(TestModule::testfixture("java14-dacapo-biojava_3.is.xz"), 1, 30000_ns);

    // Warmstart: L2C completed 59469 transactions with 49298 unique lines: 40376 fills to invalid way, 10170 hits, 8923
    // evicts, 1 duplicate fills
    // Warmstart: SLC completed 8923 fills with 8923 unique lines: 8923 fills to invalid way,
    // 1 hits, 0 evicts, 0 duplicate fills
    EXPECT_THAT(*capture, HasSubstr("8923 evicts"));
    EXPECT_THAT(*capture, HasSubstr("SLC completed 8923 fills"));
}

TEST_F(RogueCoreIntegrationTest, DumpAtEnd)
{
    // sunflow trace is the only one in this stash that has fdiv and fsqrt
    get_environment()->parameters.add_setting(
        "sys.soc_0.core_0.l2c.warmup_ms", TestModule::testfixture("java14-dacapo-biojava_3.ms.xz"), "test bench");

    get_environment()->parameters.add_setting("sys.soc_0.core_0.bpu.dump_at_end", "true", "test bench");
    get_environment()->parameters.add_setting("sys.soc_0.core_0.gpc.dump_at_end", "true", "test bench");
    get_environment()->parameters.add_setting("sys.soc_0.core_0.icf.dump_at_end", "true", "test bench");
    get_environment()->parameters.add_setting("sys.soc_0.core_0.icf.ifb.dump_at_end", "true", "test bench");
    get_environment()->parameters.add_setting("sys.soc_0.core_0.idr.dump_at_end", "true", "test bench");
    get_environment()->parameters.add_setting("sys.soc_0.core_0.ixu.dump_at_end", "true", "test bench");
    get_environment()->parameters.add_setting("sys.soc_0.core_0.lsu.prefetcher.pcp.dump_at_end", "true", "test bench");
    get_environment()->parameters.add_setting("sys.soc_0.core_0.lsu.sf_predictor.dump_at_end", "true", "test bench");
    get_environment()->parameters.add_setting("sys.soc_0.core_0.lsu.dump_at_end", "true", "test bench");
    get_environment()->parameters.add_setting("sys.soc_0.core_0.l2c.dump_at_end", "true", "test bench");
    get_environment()->parameters.add_setting("sys.soc_0.core_0.immu.dump_at_end", "true", "test bench");
    get_environment()->parameters.add_setting("sys.soc_0.core_0.dmmu.dump_at_end", "true", "test bench");
    get_environment()->parameters.add_setting("sys.soc_0.core_0.ixu.xu.A_P0.dump_at_end", "true", "test bench");

    run_instruction_stream(TestModule::testfixture("java11-dacapo-sunflow_97_20200601_4dbec4.is.xz"), 1, 50000_ns);
}

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
