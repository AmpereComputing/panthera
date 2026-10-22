//
// Copyright (c) 2018-2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include "ampere/sc/module.h"
#include "ampere/sc/testing/test.h"

namespace ampere {
namespace sc {

class ModuleTest : public Test
{};

class AModule : public Module
{
public:
    AModule(sc_core::sc_module_name name) : Module(name) {}

    bool run() override
    {
        ++run_count;
        return run_next_time;
    }

    int run_count{0};
    bool run_next_time{false};
};

TEST_F(ModuleTest, ModuleRunsOnClockTick)
{
    AModule a("a");
    tick();
    EXPECT_EQ(a.run_count, 1);
}

TEST_F(ModuleTest, RunReturningFalseOnlyRunsOnce)
{
    AModule a("a");
    tick();
    EXPECT_EQ(a.run_count, 1);
    tick();
    tick();
    tick();
    EXPECT_EQ(a.run_count, 1);
}

TEST_F(ModuleTest, RunReturningTrueAlwaysRuns)
{
    AModule a("a");
    a.run_next_time = true;

    for (int i = 0; i < 7; i++)
        tick();

    EXPECT_EQ(a.run_count, 7);
}

TEST_F(ModuleTest, ThrowError)
{
    AModule a("a");
    ASSERT_ANY_THROW(a.throw_error("bad", __FILE__, __LINE__));
}

TEST_F(ModuleTest, UseAlternateClock)
{
    Environment::get_root()->parameters.add_setting("a.clock_period", "0.3ns", "test");
    Module a(sc_core::sc_module_name("a"));
    EXPECT_EQ(a.get_clock()->get_period(), sc_core::sc_time(0.3, sc_core::SC_NS));
}

TEST_F(ModuleTest, CyclesFromNow)
{
    AModule a("a");
    auto time_5_ns = sc_core::sc_time{5, sc_core::SC_NS};
    auto time_2_ns = sc_core::sc_time{2, sc_core::SC_NS};
    EXPECT_EQ(a.cycles_from_now(5), time_5_ns);
    EXPECT_EQ(a.cycles_from_now(2), time_2_ns);
}

struct Child : Module
{
    Child(const sc_core::sc_module_name& name) : Module{name} {}
};

struct Parent : Module
{
    Parent(const sc_core::sc_module_name& name) : Module{name} {}
    Child child{"child"};
};

TEST_F(ModuleTest, ModuleClockWhichMatchesParentKeepsParent)
{
    auto& params = Environment::get_root()->parameters;

    params.add_setting("parent.clock_period", "1ns", "test");
    params.add_setting("parent.child.clock_period", "1ns", "test");

    Parent parent{"parent"};
    EXPECT_EQ(parent.get_clock(), parent.child.get_clock());
}

} // namespace sc
} // namespace ampere

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
