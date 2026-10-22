//
// Copyright (c) 2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include "ampere/sc/connect.h"
#include "ampere/sc/fifo/fifo.h"
#include "ampere/sc/module.h"
#include "ampere/sc/testing/test.h"
#include "ampere/sc/testing/clock_dummy.h"

using namespace ampere::sc;

class CxConnectTest : public Test
{
protected:
    ClockDummy src_clock{"src_clock"};
    ClockDummy tgt_clock{"tgt_clock"};

    void src_tick() { src_clock.tick(); }
    void tgt_tick() { tgt_clock.tick(); }
};

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

class TgtModule : public Module
{
public:
    TgtModule(const sc_core::sc_module_name& name, Clock* clock) : Module(name, clock) {}
    In<int> in;

    int recv() { return in->can_take() ? in->take() : -1; }
};

TEST_F(CxConnectTest, CanConnectAcrossClocksWithCapacity)
{
    SrcModule a("a", &src_clock);
    TgtModule b("b", &tgt_clock);

    connect(a.out, b.in).capacity(3);
}

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
