//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/sys/sys.h"
#include "ampere/sc/testing/test_module.h"

using namespace ::testing;

TEST_F(IntegrationTest, testDoublePumpI2F)
{
    get_environment()->parameters.add_setting(
        "sys.soc_0.core_0.instruction_stream_name", TestModule::testfixture("dblpmpi2f_0.is.xz"), "test bench");
    get_environment()->parameters.add_setting("sys.soc_0.core_0.gpc.max_instructions", 10700, "test bench");

    // >= Connor, as trace uses SVE
    get_environment()->parameters.load(TestModule::testconfig("sys/domino.cfg"));

    Sys sys("sys", clock, get_environment());

    run_for(20000_ns);

    EXPECT_GE(event_count("sys.soc_0.core_0.ixu.double_pump_first_issue"), 5700);
    EXPECT_GE(event_count("sys.soc_0.core_0.ixu.double_pump_second_issue"), 5700);
    EXPECT_GE(event_count("sys.soc_0.core_0.ixu.i2f_shelf"), 800);
    EXPECT_LE(event_count("sys.soc_0.core_0.ixu.i2f_shelf"), 2178);
    EXPECT_GE(event_count("sys.soc_0.core_0.ixu.block_fsu_pipe_for_i2f"), 2868);
}
