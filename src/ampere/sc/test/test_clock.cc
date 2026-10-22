//
// Copyright (c) 2018-2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/sc/clock.h"
#include "ampere/sc/testing/test.h"

#include <gtest/gtest.h>
#include "ampere/sc/testing/clock_dummy.h"

namespace ampere {
namespace sc {

struct TestRunnable : public Runnable
{
    TestRunnable(Clock& clock) : clock(clock) {}

    bool run() override
    {
        run_history.push_back(clock.get_cycle());
        return false;
    }

    void init() override { init_history.push_back(clock.get_cycle()); }

    Clock& clock;
    std::vector<Cycle> run_history;
    std::vector<Cycle> init_history;
};

struct ClockTest : public Test
{};

TEST_F(ClockTest, TickAdvancesClockCycle)
{
    ClockDummy clock;
    EXPECT_EQ(clock.get_cycle(), 0);
    clock.tick();
    EXPECT_EQ(clock.get_cycle(), 1);
}

TEST_F(ClockTest, RunnableRunsWhenScheduled)
{
    ClockDummy clock;
    TestRunnable runnable(clock);
    clock.schedule(&runnable, 1);
    clock.tick();
    clock.tick();
    std::vector<Cycle> expected_history = {1};
    EXPECT_EQ(runnable.run_history, expected_history);
}

TEST_F(ClockTest, RunnableRunsInTimeOrder)
{
    ClockDummy clock;
    TestRunnable runnable(clock);
    clock.schedule(&runnable, 3);
    clock.schedule(&runnable, 2);
    clock.schedule(&runnable, 1);
    for (int i = 0; i < 4; i++)
        clock.tick();
    std::vector<Cycle> expected_history = {1, 2, 3};
    EXPECT_EQ(runnable.run_history, expected_history);
}

TEST_F(ClockTest, RunnableWontRunMoreThanOnceACycle)
{
    ClockDummy clock;
    TestRunnable runnable(clock);
    clock.schedule(&runnable, 1);
    clock.schedule(&runnable, 1);
    clock.tick();
    clock.tick();
    std::vector<Cycle> expected_history = {1};
    EXPECT_EQ(runnable.run_history, expected_history);
}

TEST_F(ClockTest, RunnableInitializes)
{
    ClockDummy clock;
    TestRunnable runnable(clock);
    clock.add_runnable(&runnable);
    clock.tick();
    std::vector<Cycle> expected_history = {0};
    EXPECT_EQ(runnable.init_history, expected_history);
}

} // namespace sc
} // namespace ampere

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
