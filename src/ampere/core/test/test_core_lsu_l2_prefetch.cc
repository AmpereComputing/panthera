//
// Copyright (c) 2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/core/test/core_integration.h"

struct LSUL2PrefetchesTest : ConnorCoreIntegrationTest
{
    void SetUp() override
    {
        ConnorCoreIntegrationTest::SetUp();
        get_environment()->parameters.add_setting(
            "sys.soc_0.core_0.lsu.l2_prefetches_for_ofb_full_demand", true, "test bench");
    }

    auto l2_prefetches() { return event_count("sys.soc_0.core_0.lsu.l2_prefetch"); }
    auto ofb_full() { return event_count("sys.soc_0.core_0.lsu.ofb_full"); }
};

TEST_F(LSUL2PrefetchesTest, DemandsOnly)
{
    run_instruction_stream(TestModule::testfixture("505.mcf-1.59701_chop.is.xz"), 20000, 40000_ns);

    EXPECT_GE(l2_prefetches(), 4);
    EXPECT_LT(ofb_full(), 450);
}

TEST_F(LSUL2PrefetchesTest, AllRequests)
{
    get_environment()->parameters.add_setting(
        "sys.soc_0.core_0.lsu.l2_prefetches_for_ofb_full_prefetch", true, "test bench");

    run_instruction_stream(TestModule::testfixture("505.mcf-1.59701_chop.is.xz"), 10000, 20000_ns);

    EXPECT_GT(l2_prefetches(), 5);
    EXPECT_LT(ofb_full(), 525);
}

TEST_F(LSUL2PrefetchesTest, AllRequestsToPrq)
{
    get_environment()->parameters.add_setting(
        "sys.soc_0.core_0.lsu.l2_prefetches_for_ofb_full_prefetch", true, "test bench");
    get_environment()->parameters.add_setting(
        "sys.soc_0.core_0.lsu.l2_prefetches_all_to_l2_prefetch_queue", true, "test bench");

    run_instruction_stream(TestModule::testfixture("505.mcf-1.59701_chop.is.xz"), 10000, 20000_ns);

    EXPECT_GT(l2_prefetches(), 10);
    EXPECT_LT(ofb_full(), 525);
}
