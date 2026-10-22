//
// Copyright (c) 2018-2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include <sstream>
#include <fmt/format.h>
#include "ampere/sc/module.h"
#include "ampere/sc/report.h"
#include "ampere/sc/ancestor.h"
#include "ampere/sc/param.h"
#ifndef NDEBUG
#include "ampere/sc/debug_registry.h"
#endif

namespace ampere::sc {

Module::Module(const sc_core::sc_module_name& name, Clock* clock_in, Environment* env_in) : sc_core::sc_module(name)
{
    environment = env_in ? env_in : get_environment_from_parent();

    auto clock_period = Param<sc_core::sc_time>("clock_period", "clock period", sc_core::SC_ZERO_TIME);
    auto clock_from_object =
        Param<std::string>("clock_from_object", "obtain a clock from the given (global) object name", "");

    if (!clock_from_object.val().empty()) {
        bool found_clock = false;
        auto* obj = sc_core::sc_find_object(clock_from_object.val().c_str());

        if (obj) {
            auto* mod = dynamic_cast<Module*>(obj);
            if (mod) {
                clock = mod->get_clock();
                found_clock = true;
            }
        }

        if (!found_clock)
            AMPERE_ERROR("could not obtain a clock from \"{}\"; not a Module?", clock_from_object.val());
    } else if (clock_period.val() != sc_core::SC_ZERO_TIME) {
        auto* parent_clock = get_clock_from_parent();

        if (clock_period.val() == parent_clock->get_period()) {
            // If the period matches the parent, just use the parent.
            //
            // NOTE: I'm not actually sure this is a good idea, but it's backwardly compatible
            // with what we've been doing.
            clock = parent_clock;
        } else {
            clock = new Clock("clock", clock_period.val());
            clock_owner = true;
        }
    } else if (clock_in) {
        clock = clock_in;
    } else {
        clock = get_clock_from_parent();
    }

    AMPERE_ASSERT(clock, "could not find a clock");

    clock->add_runnable(this);

    dump_at_end = Param<bool>("dump_at_end", "dump debug information from module at end of simulation", false);

#ifndef NDEBUG
    DebugRegistry::setup_module(*this);
#endif
}

Module::~Module()
{
    if (clock_owner)
        delete clock;
}

Clock* Module::get_clock_from_parent()
{
    if (auto* m = get_nearest_ancestor<Module>(this); m) {
        if (auto* clk = m->get_clock(); clk)
            return clk;
    }

    return Clock::get_root();
}

Environment* Module::get_environment_from_parent()
{
    if (auto* m = get_nearest_ancestor<Module>(this); m) {
        if (auto* env = m->get_environment(); env)
            return env;
    }

    return Environment::get_root();
}

void Module::throw_error(std::string message, const char* filename, int line)
{
    throw std::runtime_error{fmt::format("Error in {}: {} at {}:{}", this->name(), message, filename, line)};
}

void Module::end_of_simulation()
{
    if (dump_at_end)
        dump();
}

} // namespace ampere::sc

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
