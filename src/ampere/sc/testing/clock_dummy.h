//
// Copyright (c) 2018-2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include "ampere/sc/clock.h"
#include "ampere/sc/module.h"
#include "ampere/sc/visit.h"
#include <gmock/gmock.h>

namespace ampere::sc {

class ClockDummy : public Clock
{
public:
    ClockDummy(bool run_always_ = false) : Clock("clock", {1, sc_core::SC_NS}) { run_always(run_always_); }
    ClockDummy(const char* name) : Clock(name, {1, sc_core::SC_NS}) {}

    void tick(int n = 1)
    {
        if (!initialized)
            initialize();
        while (n--)
            Clock::tick();
    }

    void schedule(Runnable* r, CycleDelta cycles_from_now) override
    {
        if (!initialized)
            initialize();
        Clock::schedule(r, cycles_from_now);
    }

    void initialize()
    {
        initialized = true;
        Clock::initialize();

        // Since we're running without SystemC, we need to run these hooks manually.
        std::function<void(Module*)> run_end_of_elaboration = [](Module* m) {
            m->end_of_elaboration();
        };
        std::function<void(Module*)> run_start_of_simulation = [](Module* m) {
            m->start_of_simulation();
        };
        visit_objects(run_end_of_elaboration);
        visit_objects(run_start_of_simulation);
    }

private:
    bool initialized{false};
};

class ClockMock : public Clock
{
public:
    ClockMock() : Clock("clock") {}
    MOCK_METHOD(void, schedule, (Runnable*, CycleDelta), (override));
};

} // namespace ampere::sc

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
