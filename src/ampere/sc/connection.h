//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include <functional>
#include <string>
#include <systemc>
#include <vector>

#include "ampere/sc/clock.h"
#include "ampere/sc/cycle.h"
#include "ampere/sc/runnable.h"

namespace ampere::sc {

class Connection : public sc_core::sc_object
{
public:
    explicit Connection(Clock& clock);
    explicit Connection(const char* name, Clock& clock);
    virtual void add_target(Runnable* to, Runnable* from = nullptr);

    const char* kind() const override { return "ampere::sc::Connection"; }

    CycleDelta validate_latency{0};
    CycleDelta validate_return_latency{0};
    std::function<size_t()> validate_credits;
    std::function<std::string()> validate_credits_pool_name;

protected:
    Clock& clock;
    std::vector<Runnable*> targets;
    std::string first_port_name; // only used for debug messages

    void schedule_targets(CycleDelta cycles_from_now);
};

inline void Connection::schedule_targets(CycleDelta cycles_from_now)
{
    for (auto& t : targets)
        clock.schedule(t, cycles_from_now);
}

} // namespace ampere::sc
