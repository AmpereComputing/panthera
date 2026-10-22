//
// Copyright (c) 2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/core/test/core_integration.h"

struct FsuUVWIntegrationTest : NebulaCoreIntegrationTest
{
    auto U_P0() { return event_count("sys.soc_0.core_0.ixu.xu.U_P0.uop_execute"); }
    auto V_P0() { return event_count("sys.soc_0.core_0.ixu.xu.V_P0.uop_execute"); }
    auto W_P0() { return event_count("sys.soc_0.core_0.ixu.xu.W_P0.uop_execute"); }
    auto X_P0() { return event_count("sys.soc_0.core_0.ixu.xu.X_P0.uop_execute"); }
    auto Y_P0() { return event_count("sys.soc_0.core_0.ixu.xu.Y_P0.uop_execute"); }
};

struct FsuUW : FsuUVWIntegrationTest
{
    void SetUp() override
    {
        NebulaCoreIntegrationTest::SetUp();
        get_environment()->parameters.add_setting("sys.soc_0.core_0.idr.u_pipe_enable", true, "test bench");
        get_environment()->parameters.add_setting("sys.soc_0.core_0.idr.v_pipe_enable", false, "test bench");
        get_environment()->parameters.add_setting("sys.soc_0.core_0.idr.w_pipe_enable", true, "test bench");
        get_environment()->parameters.add_setting("sys.soc_0.core_0.idr.x_pipe_by_enable", false, "test bench");
        get_environment()->parameters.add_setting("sys.soc_0.core_0.idr.y_pipe_by_enable", false, "test bench");
        get_environment()->event_stream->enable();
    }
};

TEST_F(FsuUW, TestPipes_UW)
{
    run_instruction_stream(TestModule::testfixture("sve2-loop_110.elf_0_20240305_295c6b.is.xz"), 1000, 2000_ns);

    EXPECT_GT(U_P0(), 4);
    EXPECT_EQ(V_P0(), 0);
    EXPECT_GT(W_P0(), 10);

    EXPECT_GT(X_P0(), 100);
    EXPECT_GT(Y_P0(), 100);
}

struct FsuXV : FsuUVWIntegrationTest
{
    void SetUp() override
    {
        NebulaCoreIntegrationTest::SetUp();
        get_environment()->parameters.add_setting("sys.soc_0.core_0.idr.u_pipe_enable", false, "test bench");
        get_environment()->parameters.add_setting("sys.soc_0.core_0.idr.v_pipe_enable", true, "test bench");
        get_environment()->parameters.add_setting("sys.soc_0.core_0.idr.w_pipe_enable", false, "test bench");
        get_environment()->parameters.add_setting("sys.soc_0.core_0.idr.x_pipe_by_enable", true, "test bench");
        get_environment()->parameters.add_setting("sys.soc_0.core_0.idr.y_pipe_by_enable", false, "test bench");
        get_environment()->event_stream->enable();
    }
};

TEST_F(FsuXV, TestPipes_XV)
{
    run_instruction_stream(TestModule::testfixture("sve2-loop_110.elf_0_20240305_295c6b.is.xz"), 1000, 2000_ns);

    EXPECT_EQ(U_P0(), 0);
    EXPECT_GT(V_P0(), 10);
    EXPECT_EQ(W_P0(), 0);

    EXPECT_GT(X_P0(), 100);
    EXPECT_GT(Y_P0(), 100);
}
