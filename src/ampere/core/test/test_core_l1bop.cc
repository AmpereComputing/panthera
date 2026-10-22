//
// Copyright (c) 2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/sys/sys.h"
#include "ampere/sc/testing/test_module.h"

using namespace ::testing;

struct L1BOPIntegrationTest : IntegrationTest
{
    void SetUp() override
    {
        get_environment()->parameters.load(TestModule::testconfig("sys/connor-core-test.cfg"));
        get_environment()->parameters.add_setting("sys.soc_0.core_0.gpc.max_instructions", 50000, "test");
        get_environment()->parameters.add_setting(
            "sys.soc_0.core_0.lsu.prefetcher:override-cfg", TestModule::testconfig("core/connor/l1bop.cfg"), "test");
        get_environment()->parameters.add_setting("sys.soc_0.core_0.lsu.prefetcher.score_widgets", 1, "test");
    }

    uint64_t l1bop_launch() { return event_count("sys.soc_0.core_0.lsu.prefetcher.launch"); }
};

TEST_F(L1BOPIntegrationTest, testL1BOPSequentialStream)
{
    get_environment()->parameters.add_setting(
        "sys.soc_0.core_0.instruction_stream_name", TestModule::testfixture("sequential-stream_0.is.xz"), "test");

    Sys sys("sys", clock, get_environment());

    run_for(20000_ns);

    EXPECT_GT(l1bop_launch(), 0);
    EXPECT_NEAR(simulated_ipc(), 3.8, 0.25);
}
