//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#pragma once

#include <string>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <sysc/kernel/sc_simcontext.h>
#include <sysc/kernel/sc_time.h>

#include "ampere/sc/clock.h"
#include "ampere/sc/environment.h"

namespace ampere::sc {

/** A clock for the testing infrastructure.
 *
 *  This provides "tick()" as a back-door into ticking a clock without having
 *  to deal with SystemC events and time.
 *
 *  Vs. ClockDummy: ClockDummy was sort of the first version of this, before
 *  SystemC elaboration semantics were built into Test, so it has additional
 *  routines for manual clock initialization. Probably ClockDummy should go
 *  away, but there were some uses I didn't get to cleaning up yet.
 */
class TestClock : public Clock
{
public:
    TestClock(bool run_always_ = false) : Clock{"test_clock", 1_ns} { run_always(run_always_); }

    void tick(int n = 1)
    {
        while (n--)
            Clock::tick();
    }
};

/** A base test fixture for just about any test.
 *
 *  If you are writing a test that involves any simulation object (modules,
 *  ports, connections, etc.), you should start with "sc::Test" or a child of
 *  "sc::Test".
 *
 *  (This class, "TestT" is just a templated parent of "Test" with an option
 *  for which Google Test it derives from. Consider this class to be more or
 *  less internal. I've put the description on this class simply because it
 *  has all the body code.)
 *
 *  What does this class do? It creates/destroys a fresh SystemC context for
 *  each test invocation, along with a default Environment and Clock. It also
 *  provides a means to perform SystemC post-elaboration work: namely completing
 *  all port bindings and registering ports with channels. Not all tests really
 *  *need* this, but an increasing number do, so it makes sense to build this
 *  in to the infrastructure.
 *
 *  What it doesn't do: it doesn't actually advance SystemC time. Instead it
 *  provides a back-door "tick" of the clock that in almost all cases is the
 *  simplest and most useful thing to do (the exception might be some tests
 *  that have multiple clocks).
 *
 *  A typical test structure might look like this:
 *
 *      struct MyTestSuite : public sc::Test {};
 *
 *      TEST_F(MyTestSuite, SomeTest)
 *      {
 *         Module my_module_a{"my_module_a"};
 *         Module my_module_b{"my_module_b"};
 *
 *         connect(my_module_a.out, my_module_b.in);
 *         start_simulation(); // OPTIONAL: see below
 *
 *         tick(10); // advance 10 cycles
 *
 *         // test expectations
 *         EXPECT_EQ(...)
 *      }
 *
 *  The call to "start_simulation()" occurs after all modules have been created
 *  and connected. It is optional most of the time because the first call to
 *  "tick()" will check if elaboration has been completed and if not, it will
 *  cause it to complete. But there are some kinds of tests that don't start with
 *  "tick()" and need to be explicit.
 */
template<typename GoogleTestBase = ::testing::Test>
struct TestT : public GoogleTestBase
{
protected:
    TestT()
    {
        sc_core::sc_curr_simcontext = new sc_core::sc_simcontext;
        Environment::set_root(std::make_unique<Environment>("testing"));
        clock = new TestClock{};
        Clock::set_root(std::unique_ptr<Clock>{clock});
    }

    ~TestT()
    {
        Clock::set_root(nullptr);
        Environment::set_root(nullptr);
        delete sc_core::sc_curr_simcontext;
        sc_core::sc_curr_simcontext = sc_core::sc_default_global_context;
    }

    Environment* get_environment() { return Environment::get_root(); }

    void start_simulation() { sc_core::sc_start(sc_core::sc_time{0, sc_core::SC_NS}); }

    void tick(int n = 1)
    {
        if (sc_core::sc_get_status() == sc_core::SC_ELABORATION)
            start_simulation();
        clock->tick(n);
    }

    Clock* get_clock() { return clock; }
    Cycle get_cycle() const { return clock->get_cycle(); }

    TestClock* clock;
};

/** Test: start here for writing unit tests.
 *
 *  See the description of TestT for details.
 */
using Test = TestT<>;

/** TestWithParam: a variant of Test if you want to use Google Test's
 *  TestWithParam for writing parametrized tests.
 */
template<typename T>
using TestWithParam = TestT<::testing::TestWithParam<T>>;

} // namespace ampere::sc
