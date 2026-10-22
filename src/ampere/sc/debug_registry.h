//
// Copyright (c) 2025 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include <map>
#include <string>
#include <utility>
#include <systemc>

#include "ampere/sc/clock.h"
#include "ampere/sc/cycle.h"

namespace ampere::sc {

class Module;
class Environment;

class DebugRegistry
{
    enum Mode {
        NONE,
        FILTERED,
        ALL
    };
    mutable std::map<std::string, Mode> modules;
    mutable std::map<std::pair<std::string, std::string>, bool> categories;

    inline static auto key(const char *name, const char *category)
    {
        return std::make_pair<std::string, std::string>(name, category);
    }

    bool all_enabled{false};
    std::optional<Cycle> start;
    std::optional<Cycle> stop;

    bool within_enabled_range(const Clock *clock) const;

public:
    void add(const sc_core::sc_module &module, const char *category);

    bool enabled(const sc_core::sc_module &module, const Clock *clock = nullptr) const;
    bool enabled(const sc_core::sc_module &module, const char *category, const Clock *clock = nullptr) const;

    void set_enable_all() { all_enabled = true; }
    void set_enabled_at(Cycle cycle) { start = cycle; }
    void set_disabled_at(Cycle cycle) { stop = cycle; }

    static void setup_main(Environment &env);
    static void setup_main_with_start_stop(Environment &env);

    static void setup_module(const Module &m);
};

} // namespace ampere::sc

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
