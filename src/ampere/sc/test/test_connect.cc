//
// Copyright (c) 2018-2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include <gtest/gtest.h>
#include <stdexcept>
#include <array>

#include "ampere/sc/connect.h"
#include "ampere/sc/fifo/fifo.h"
#include "ampere/sc/module.h"
#include "ampere/sc/port_multiplex.h"
#include "ampere/sc/testing/test.h"

using namespace ampere::sc;

class ConnectTest : public Test
{};

class SrcModule : public Module
{
public:
    SrcModule(const sc_core::sc_module_name& name, Clock* clock) : Module(name, clock) {}
    Out<int> out;

    bool send(int value)
    {
        if (out->can_put()) {
            out->put(value);
            return true;
        } else {
            return false;
        }
    }
};

class SrcOptionalModule : public Module
{
public:
    SrcOptionalModule(const sc_core::sc_module_name& name, Clock* clock) : Module(name, clock) {}
    OutOpt<int> out;

    bool send(int value)
    {
        if (out->can_put()) {
            out->put(value);
            return true;
        } else {
            return false;
        }
    }
};

class TgtModule : public Module
{
public:
    TgtModule(const sc_core::sc_module_name& name, Clock* clock) : Module(name, clock) {}
    In<int> in;

    int recv() { return in->can_take() ? in->take() : -1; }
};

class TgtOptionalModule : public Module
{
public:
    TgtOptionalModule(const sc_core::sc_module_name& name, Clock* clock) : Module(name, clock) {}
    InOpt<int> in;

    int recv() { return in->can_take() ? in->take() : -1; }
};

class RdrModule : public Module
{
public:
    RdrModule(const sc_core::sc_module_name& name, Clock* clock) : Module(name, clock) {}
    Read<int> read;

    int recv() { return read->can_read() ? read->read() : -1; }
};

template<bool ZERO = false>
class SelfConnectModule : public Module
{
public:
    OutIn<int, ZERO> connection{"something"};
    SelfConnectModule(const sc_core::sc_module_name& name, Clock* clock) : Module(name, clock) {}
};

struct RequiredConnectTest : ConnectTest
{
    SrcModule a;
    TgtModule b;

    RequiredConnectTest() : a("a", clock), b("b", clock) {}
};

TEST_F(RequiredConnectTest, ConnectTwoModulesWithDelay)
{
    connect(a.out, b.in);

    start_simulation();

    EXPECT_TRUE(a.send(21));
    tick();
    EXPECT_EQ(b.recv(), 21);
    EXPECT_TRUE(a.send(42));
    tick();
    EXPECT_EQ(b.recv(), 42);
}

TEST_F(RequiredConnectTest, ConnectExplicitBindTwoModulesWithDelay)
{
    auto settings = connect(a.out, b.in).bind();
    EXPECT_EQ(settings.get_latency(), 1);

    start_simulation();

    EXPECT_TRUE(a.send(21));
    tick();
    EXPECT_EQ(b.recv(), 21);
    EXPECT_TRUE(a.send(42));
    tick();
    EXPECT_EQ(b.recv(), 42);
}

TEST_F(RequiredConnectTest, ConnectTwoModulesWithAQueue)
{
    connect(a.out, b.in).capacity(3);

    start_simulation();

    EXPECT_TRUE(a.send(19));
    tick();
    EXPECT_TRUE(a.send(20));
    tick();
    EXPECT_TRUE(a.send(21));

    tick(3);

    EXPECT_EQ(b.recv(), 19);
    EXPECT_EQ(b.recv(), 20);
    EXPECT_EQ(b.recv(), 21);
}

TEST_F(RequiredConnectTest, ConnectTwoModulesWithAQueueAndExplicitReturn)
{
    connect(a.out, b.in).capacity(3).return_latency(10);

    start_simulation();

    EXPECT_TRUE(a.send(19));
    tick();
    EXPECT_TRUE(a.send(20));
    tick();
    EXPECT_TRUE(a.send(21));

    tick(3);

    EXPECT_EQ(b.recv(), 19);
    EXPECT_EQ(b.recv(), 20);
    EXPECT_EQ(b.recv(), 21);

    tick(9);
    EXPECT_FALSE(a.send(22));
    tick();
    EXPECT_TRUE(a.send(22));
}

TEST_F(RequiredConnectTest, ConnectTwoModulesWithABandwidthTwoDelay)
{
    connect(a.out, b.in).bandwidth(2);

    start_simulation();

    EXPECT_TRUE(a.send(91));
    EXPECT_TRUE(a.send(92));
    tick();
    EXPECT_TRUE(a.send(93));
    EXPECT_TRUE(a.send(94));
    EXPECT_EQ(b.recv(), 91);
    EXPECT_EQ(b.recv(), 92);
    tick();
    EXPECT_EQ(b.recv(), 93);
    EXPECT_EQ(b.recv(), 94);
}

TEST_F(RequiredConnectTest, ConnectTwoModulesWithABandwidthTwoQueue)
{
    connect(a.out, b.in).capacity(3).bandwidth(2);

    start_simulation();

    EXPECT_TRUE(a.send(4444));
    EXPECT_TRUE(a.send(5555));

    tick(3);

    EXPECT_EQ(b.recv(), 4444);
    EXPECT_EQ(b.recv(), 5555);
}

TEST_F(RequiredConnectTest, ConnectTwoModulesWithDifferingBandwidths)
{
    connect(a.out, b.in).capacity(4).rd_bandwidth(2).wr_bandwidth(4);

    start_simulation();

    EXPECT_TRUE(a.send(4444));
    EXPECT_TRUE(a.send(5555));
    EXPECT_TRUE(a.send(6666));
    EXPECT_TRUE(a.send(7777));

    tick(3);

    EXPECT_EQ(b.recv(), 4444);
    EXPECT_EQ(b.recv(), 5555);
    EXPECT_EQ(b.recv(), -1);

    tick();

    EXPECT_EQ(b.recv(), 6666);
    EXPECT_EQ(b.recv(), 7777);
}

TEST_F(ConnectTest, ConnectTwoModulesWithBroadcast)
{
    SrcModule a("a", clock);
    RdrModule b("b", clock);

    connect(a.out, {&b.read});

    start_simulation();

    EXPECT_TRUE(a.send(21));
    tick();
    EXPECT_EQ(b.recv(), 21);
    EXPECT_EQ(b.recv(), 21);
    EXPECT_TRUE(a.send(42));
    tick();
    EXPECT_EQ(b.recv(), 42);
    EXPECT_EQ(b.recv(), 42);
}

TEST_F(ConnectTest, ConnectThreeModulesWithBroadcast)
{
    using std::vector;
    SrcModule a("a", clock);
    RdrModule x("x", clock);
    RdrModule y("y", clock);

    connect(a.out, {&x.read, &y.read});

    start_simulation();

    EXPECT_TRUE(a.send(21));
    tick();
    EXPECT_EQ(x.recv(), 21);
    EXPECT_EQ(y.recv(), 21);
    EXPECT_TRUE(a.send(42));
    tick();
    EXPECT_EQ(x.recv(), 42);
    EXPECT_EQ(y.recv(), 42);
}

TEST_F(RequiredConnectTest, ConnectTwoModulesWithZDelay)
{
    connect(a.out, b.in).latency(0);

    start_simulation();

    EXPECT_TRUE(a.send(21));
    EXPECT_EQ(b.recv(), 21);
}

TEST_F(ConnectTest, ConnectTwoOptionalModulesWithDelay)
{
    SrcOptionalModule a("a", clock);
    TgtOptionalModule b("b", clock);

    connect(a.out, b.in);

    start_simulation();

    EXPECT_TRUE(a.send(21));
    tick();
    EXPECT_EQ(b.recv(), 21);
    EXPECT_TRUE(a.send(42));
    tick();
    EXPECT_EQ(b.recv(), 42);
}

TEST_F(ConnectTest, ConnectOneOptionalModulesWithDelay)
{
    SrcOptionalModule a("a", clock);
    TgtModule b("b", clock);

    connect(a.out, b.in);

    start_simulation();

    EXPECT_TRUE(a.send(21));
    tick();
    EXPECT_EQ(b.recv(), 21);
    EXPECT_TRUE(a.send(42));
    tick();
    EXPECT_EQ(b.recv(), 42);
}

TEST_F(ConnectTest, NotConnectTwoOptionalModules)
{
    SrcOptionalModule a("a", clock);
    TgtOptionalModule b("b", clock);

    start_simulation();

    // NOTE: bind_count can be slow
    EXPECT_EQ(a.out.bind_count(), 0);
    EXPECT_EQ(b.in.bind_count(), 0);

    // Calls will raise a "port is not bound" exception
    EXPECT_THROW({ a.send(42); }, std::exception);
    EXPECT_THROW({ b.recv(); }, std::exception);
}

TEST_F(ConnectTest, SelfConnection)
{
    SelfConnectModule<> a("a", clock);
    connect(a.connection);

    start_simulation();

    EXPECT_TRUE(a.connection.can_put());
    a.connection.put(1);

    tick();

    EXPECT_TRUE(a.connection.can_take());
    EXPECT_EQ(a.connection.take(), 1);
}

TEST_F(ConnectTest, SelfConnectionWithZDelay)
{
    SelfConnectModule<true> a("a", clock);
    connect(a.connection).latency(0);

    start_simulation();

    EXPECT_TRUE(a.connection.can_put());
    a.connection.put(1);

    EXPECT_TRUE(a.connection.can_take());
    EXPECT_EQ(a.connection.take(), 1);
}

TEST_F(ConnectTest, SelfConnectionWithZDelayNotAllowed)
{
    SelfConnectModule<> a("a", clock);
    connect(a.connection).latency(0);

    EXPECT_THROW({ start_simulation(); }, std::exception);
}

struct ConnectSettingsTest : RequiredConnectTest
{
    void SetUp() override { get_environment()->parameters.add_setting("ab.latency", 2, "unit test"); }
};

TEST_F(ConnectSettingsTest, OverrideLatencyViaParam)
{
    auto settings = connect(a.out, b.in).name("ab").bind();
    EXPECT_EQ(settings.get_latency(), 2);
}

TEST_F(ConnectSettingsTest, ExplicitLatencyNotOverridden)
{
    auto settings = connect(a.out, b.in).name("ab").latency(4).bind();
    EXPECT_EQ(settings.get_latency(), 4);
}

template<unsigned N>
struct Multiplexer : Module
{
    std::array<In<int>, N> ins;
    Out<int> out;
    Multiplexer(const sc_core::sc_module_name& name, Clock* clock) : Module(name, clock) {}
};

struct MultiplexSingleTest : ConnectTest
{
    SrcModule a;
    Multiplexer<1> fwd;
    TgtModule b;

    MultiplexSingleTest() : a("a", clock), fwd("fwd", clock), b("b", clock) {}

    void SetUp() override
    {
        {
            auto fwder = multiplex("mux", fwd.out);
            connect(a.out, fwd.ins[0]).latency(0);
            fwder.connect_in(fwd.ins[0]);
        }

        connect(fwd.out, b.in);
    }
};

TEST_F(MultiplexSingleTest, Take)
{
    a.out->put(5);
    EXPECT_TRUE(b.in->can_take());
    EXPECT_EQ(b.in->take(), 5);
    EXPECT_FALSE(b.in->can_take());
}

TEST_F(MultiplexSingleTest, Peek)
{
    a.out->put(5);
    EXPECT_TRUE(b.in->can_take());
    EXPECT_EQ(b.in->peek(), 5);
    EXPECT_TRUE(b.in->can_take());
}

struct MultiplexMultipleTest : ConnectTest
{
    SrcModule a;
    SrcModule b;
    SrcModule c;
    Multiplexer<3> fwd;
    TgtModule d;

    MultiplexMultipleTest() : a("a", clock), b("b", clock), c("c", clock), fwd("fwd", clock), d("d", clock) {}

    void SetUp() override
    {
        {
            auto fwder = multiplex("mux", fwd.out);

            connect(a.out, fwd.ins[0]).name("a").latency(0).bandwidth(2);
            fwder.connect_in(fwd.ins[0]);

            connect(b.out, fwd.ins[1]).name("b").latency(0).bandwidth(2);
            fwder.connect_in(fwd.ins[1]);

            connect(c.out, fwd.ins[2]).name("c").latency(0).bandwidth(2);
            fwder.connect_in(fwd.ins[2]);
        }

        connect(fwd.out, d.in);

        a.out->put(5);
        a.out->put(9);
        b.out->put(6);
        c.out->put(7);
    }
};

TEST_F(MultiplexMultipleTest, TakeAllConnectionOrder)
{
    EXPECT_EQ(d.in->take(), 5);

    EXPECT_EQ(d.in->num_ready(), 3);
    EXPECT_EQ(d.in->take(), 9);

    EXPECT_EQ(d.in->num_ready(), 2);
    EXPECT_EQ(d.in->take(), 6);

    EXPECT_EQ(d.in->num_ready(), 1);
    EXPECT_EQ(d.in->take(), 7);

    EXPECT_FALSE(d.in->can_take());
}

TEST_F(MultiplexMultipleTest, PeekAllConnectionOrder)
{
    EXPECT_EQ(d.in->num_ready(), 4);

    EXPECT_EQ(d.in->peek(0), 5);
    EXPECT_EQ(d.in->peek(1), 9);
    EXPECT_EQ(d.in->peek(2), 6);
    EXPECT_EQ(d.in->peek(3), 7);

    EXPECT_TRUE(d.in->can_take());
    EXPECT_EQ(d.in->num_ready(), 4);
}

TEST_F(MultiplexMultipleTest, TakeOnePeekOthers)
{
    EXPECT_EQ(d.in->take(), 5);
    EXPECT_EQ(d.in->peek(), 9);
    EXPECT_EQ(d.in->peek(1), 6);
    EXPECT_EQ(d.in->peek(2), 7);

    EXPECT_EQ(d.in->num_ready(), 3);
}

struct MultiplexMultipleExplicitOrderTest : MultiplexMultipleTest
{
    MultiplexMultipleExplicitOrderTest() = default;
    void SetUp() override
    {
        {
            auto fwder = multiplex("mux", fwd.out);

            connect(a.out, fwd.ins[0]).name("a").latency(0).bandwidth(2);
            fwder.connect_in(fwd.ins[0], 2);

            connect(b.out, fwd.ins[1]).name("b").latency(0).bandwidth(2);
            fwder.connect_in(fwd.ins[1], 1);

            connect(c.out, fwd.ins[2]).name("c").latency(0).bandwidth(2);
            fwder.connect_in(fwd.ins[2], 0);
        }

        connect(fwd.out, d.in);
    }
};

TEST_F(MultiplexMultipleExplicitOrderTest, MultiplexAllConnectionOrder)
{
    a.out->put(5);
    a.out->put(9);
    b.out->put(6);
    c.out->put(7);

    EXPECT_EQ(d.in->take(), 7);
    EXPECT_EQ(d.in->take(), 6);
    EXPECT_EQ(d.in->take(), 5);
    EXPECT_EQ(d.in->take(), 9);
}

struct MultiplexMultipleArbiterTest : MultiplexMultipleTest
{
    bool reverse = false;

    MultiplexMultipleArbiterTest() = default;
    void SetUp() override
    {
        {
            auto fwder = multiplex("mux", fwd.out);

            fwder.arbiter([this](unsigned a, unsigned b) { return this->reverse ? b < a : a < b; });

            connect(a.out, fwd.ins[0]).name("a").latency(0).bandwidth(2);
            fwder.connect_in(fwd.ins[0], 2);

            connect(b.out, fwd.ins[1]).name("b").latency(0).bandwidth(2);
            fwder.connect_in(fwd.ins[1], 1);

            connect(c.out, fwd.ins[2]).name("c").latency(0).bandwidth(2);
            fwder.connect_in(fwd.ins[2], 0);
        }

        connect(fwd.out, d.in);
    }
};

TEST_F(MultiplexMultipleArbiterTest, AdjustArbitration)
{
    a.out->put(5);
    a.out->put(9);
    b.out->put(6);
    c.out->put(7);

    EXPECT_EQ(d.in->take(), 7);
    EXPECT_EQ(d.in->take(), 6);
    EXPECT_EQ(d.in->take(), 5);
    EXPECT_EQ(d.in->take(), 9);

    tick();
    reverse = true;

    a.out->put(5);
    a.out->put(9);
    b.out->put(6);
    c.out->put(7);

    EXPECT_EQ(d.in->take(), 5);
    EXPECT_EQ(d.in->take(), 9);
    EXPECT_EQ(d.in->take(), 6);
    EXPECT_EQ(d.in->take(), 7);
}

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
