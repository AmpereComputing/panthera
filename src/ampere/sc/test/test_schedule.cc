//
// Copyright (c) 2018-2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include <gmock/gmock.h>

#include "ampere/sc/connect.h"
#include "ampere/sc/module.h"
#include "ampere/sc/testing/test.h"

using namespace ampere::sc;
using ::testing::ElementsAre;

class ConnectScheduleTest : public Test
{};

class Sender : public Module
{
public:
    explicit Sender(sc_core::sc_module_name name) : Module(name) {}

    Out<int> out;

    bool run() override
    {
        while (!to_send.empty()) {
            if (to_send.front().first == get_clock()->get_cycle()) {
                out->put(to_send.front().second);
                to_send.pop();
            } else {
                break;
            }
        }

        return !to_send.empty();
    }

    std::queue<std::pair<Cycle, int>> to_send;
};

class Receiver : public Module
{
public:
    explicit Receiver(sc_core::sc_module_name name) : Module(name) {}

    In<int> in;

    bool run() override
    {
        if (receive_enabled) {
            if (!in->can_take()) {
                received.emplace_back(get_cycle(), -1);
                return false;
            }

            do {
                received.emplace_back(get_cycle(), in->take());
            } while (receive_all && in->can_take());
        }
        return false;
    }

    bool receive_enabled{true};
    bool receive_all{false};
    std::vector<std::pair<Cycle, int>> received;
};

class AlwaysRunningReceiver : public Module
{
public:
    explicit AlwaysRunningReceiver(sc_core::sc_module_name name) : Module(name) {}

    In<int> in;

    bool run() override
    {
        run_history.push_back(get_cycle());
        return true;
    }

    std::vector<Cycle> run_history;
};

TEST_F(ConnectScheduleTest, ModuleRunsWhenInputIsAvailableViaDelayOne)
{
    Sender a("a");
    Receiver b("b");

    connect(a.out, b.in);

    start_simulation();

    a.to_send.emplace(0, 21);
    a.to_send.emplace(3, 42);

    tick(5);

    std::vector<std::pair<Cycle, int>> expected = {{0, -1}, {1, 21}, {4, 42}};
    EXPECT_EQ(b.received, expected);
}

TEST_F(ConnectScheduleTest, ModuleRunsWhenInputIsAvailableViaDelayThree)
{
    Sender a("a");
    Receiver b("b");

    connect(a.out, b.in).latency(3);

    start_simulation();

    a.to_send.emplace(0, 21);
    a.to_send.emplace(3, 42);

    tick(7);

    std::vector<std::pair<Cycle, int>> expected = {{0, -1}, {3, 21}, {6, 42}};
    EXPECT_EQ(b.received, expected);
}

TEST_F(ConnectScheduleTest, ModuleWontRunMoreThanOnceACycleWhenTriggeredByQueueAndItself)
{
    Sender a("a");
    AlwaysRunningReceiver c("c");

    connect(a.out, c.in).capacity(3);

    start_simulation();

    a.to_send.emplace(0, 71);
    a.to_send.emplace(1, 72);
    a.to_send.emplace(2, 73);

    tick(7);

    EXPECT_THAT(c.run_history, ElementsAre(0, 1, 2, 3, 4, 5, 6));
}

using ForwardScheduleTest = ConnectScheduleTest;

template<unsigned N>
struct Forwarder : Module
{
    std::array<In<int>, N> ins;
    Out<int> out;
    Forwarder(const sc_core::sc_module_name& name, Clock* clock) : Module(name, clock) {}
};

TEST_F(ForwardScheduleTest, ModuleRunsWhenInput)
{
    Sender a("a");
    Sender b("b");
    Receiver c("c");
    Forwarder<2> fwd("fwd", get_clock());

    connect(a.out, fwd.ins[0]);
    connect(b.out, fwd.ins[1]);

    {
        auto fwder = TakerMultiplexer<int>::Setup("mux", fwd.out);
        fwder.connect_in(fwd.ins[0]);
        fwder.connect_in(fwd.ins[1]);
    }

    c.receive_all = true;
    connect(fwd.out, c.in);

    start_simulation();

    a.to_send.emplace(1, 42);
    a.to_send.emplace(5, 55);

    b.to_send.emplace(1, 13);
    b.to_send.emplace(3, 18);

    tick(7);

    std::vector<std::pair<Cycle, int>> expected = {{0, -1}, {2, 42}, {2, 13}, {4, 18}, {6, 55}};
    EXPECT_EQ(c.received, expected);
}

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
