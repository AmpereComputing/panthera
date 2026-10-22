//
// Copyright (c) 2018-2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include <boost/algorithm/string/predicate.hpp>

#include "ampere/core/test/core_integration.h"

namespace bfs = boost::filesystem;

using namespace ::testing;

TEST_F(FuriosaCoreIntegrationTest, testMispredictLatency)
{
    run_instruction_stream(TestModule::testfixture("mispredict_returns.is.xz"), 5000, 50000_ns);

    // IPC is 3/(N+2) where N is mispredict recovery. N=10 -> IPC = 0.25
    EXPECT_NEAR(simulated_ipc(), 0.25, 0.001);
}

TEST_F(RipleyCoreIntegrationTest, testMispredictLatency)
{
    run_instruction_stream(TestModule::testfixture("mispredict_returns.is.xz"), 5000, 50000_ns);

    // IPC is 3/(N+2) where N is mispredict recovery. N=10 -> IPC = 0.25
    EXPECT_NEAR(simulated_ipc(), 0.25, 0.001);
}

TEST_F(TrinityCoreIntegrationTest, testMispredictLatency)
{
    run_instruction_stream(TestModule::testfixture("mispredict_returns.is.xz"), 5000, 50000_ns);

    // IPC is 3/(N+2) where N is mispredict recovery. N=10 -> IPC = 0.25
    EXPECT_NEAR(simulated_ipc(), 0.25, 0.001);
}

TEST_F(ConnorCoreIntegrationTest, testMispredictLatency)
{
    run_instruction_stream(TestModule::testfixture("mispredict_returns.is.xz"), 5000, 50000_ns);

    // IPC is 3/(N+2) where N is mispredict recovery. N=10 -> IPC = 0.25
    EXPECT_NEAR(simulated_ipc(), 0.25, 0.001);
}

TEST_F(ConnorAICoreIntegrationTest, testMispredictLatency)
{
    run_instruction_stream(TestModule::testfixture("mispredict_returns.is.xz"), 5000, 50000_ns);

    // IPC is 3/(N+2) where N is mispredict recovery. N=11 -> IPC = 0.2308
    EXPECT_NEAR(simulated_ipc(), 0.2308, 0.001);
}

TEST_F(AlitaCoreIntegrationTest, testMispredictLatency)
{
    run_instruction_stream(TestModule::testfixture("mispredict_returns.is.xz"), 5000, 50000_ns);

    // IPC is 3/(N+2) where N is mispredict recovery. N=10 -> IPC = 0.25
    EXPECT_NEAR(simulated_ipc(), 0.25, 0.001);
}

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
