//
// Copyright (c) 2018-2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include <boost/algorithm/string/predicate.hpp>

#include "ampere/core/test/core_integration.h"

namespace bfs = boost::filesystem;

using namespace ::testing;

TEST_F(TrinityCoreIntegrationTest, testIndependentFdivsTrinity)
{
    run_instruction_stream(TestModule::testfixture("div-independent_scalar_fdiv64_0.is.xz"), 4500, 20000_ns);

    EXPECT_NEAR(simulated_ipc(), 0.113, 0.01);
}

TEST_F(RipleyCoreIntegrationTest, testIndependentFdivsRipley)
{
    run_instruction_stream(TestModule::testfixture("div-independent_scalar_fdiv64_0.is.xz"), 4500, 20000_ns);

    EXPECT_NEAR(simulated_ipc(), 0.129, 0.001);
}

TEST_F(TrinityCoreIntegrationTest, testIndependentIdivsTrinity)
{
    run_instruction_stream(TestModule::testfixture("div-independent_gpr_udiv64_0.is.xz"), 4500, 40000_ns);

    EXPECT_NEAR(simulated_ipc(), 0.0978, 0.010);
}

TEST_F(ConnorCoreIntegrationTest, testIndependentIdivsConnor)
{
    run_instruction_stream(TestModule::testfixture("div-independent_gpr_udiv64_0.is.xz"), 4500, 40000_ns);

    EXPECT_NEAR(simulated_ipc(), 0.282, 0.010);
}

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
