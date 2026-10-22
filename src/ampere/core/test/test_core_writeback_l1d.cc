//
// Copyright (c) 2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/core/test/core_integration.h"

struct WritebackL1dTest : ConnorCoreIntegrationTest
{
    void SetUp() override
    {
        ConnorCoreIntegrationTest::SetUp();
        get_environment()->parameters.add_setting("sys.soc_0.core_0.lsu.store_write_to_l2_on_miss", true, "test bench");
        get_environment()->parameters.add_setting(
            "sys.soc_0.core_0.lsu.evict_modified_via_l2_pwc", false, "test bench");
        get_environment()->event_stream->enable();
    }

    auto pwc_allocate() { return event_count("sys.soc_0.core_0.l2c.pwc.allocate"); }
    auto cache_write_modified() { return event_count("sys.soc_0.core_0.lsu.cache_write_modified"); }
};

TEST_F(WritebackL1dTest, LoadThenStore)
{
    run_instruction_stream(TestModule::testfixture("load_store_chop.is.xz"), 10000, 20000_ns);

    EXPECT_LT(pwc_allocate(), 10);
    EXPECT_GT(cache_write_modified(), 0);
}

TEST_F(WritebackL1dTest, StoreOnly)
{
    run_instruction_stream(TestModule::testfixture("independent_stores_chop.is.xz"), 10000, 20000_ns);

    EXPECT_GT(pwc_allocate(), 5);
    EXPECT_LT(cache_write_modified(), 10);
}

struct WritebackL1dThroughPWCTest : WritebackL1dTest
{
    void SetUp() override
    {
        WritebackL1dTest::SetUp();
        get_environment()->parameters.add_setting("sys.soc_0.core_0.lsu.evict_modified_via_l2_pwc", true, "test bench");
    }
};

TEST_F(WritebackL1dThroughPWCTest, LoadThenStore)
{
    run_instruction_stream(TestModule::testfixture("load_store_chop.is.xz"), 10000, 20000_ns);

    EXPECT_LT(pwc_allocate(), 10);
    EXPECT_GT(cache_write_modified(), 0);
}

TEST_F(WritebackL1dThroughPWCTest, StoreOnly)
{
    run_instruction_stream(TestModule::testfixture("independent_stores_chop.is.xz"), 10000, 20000_ns);

    EXPECT_GT(pwc_allocate(), 5);
    EXPECT_LT(cache_write_modified(), 10);
}
