//
// Copyright (c) 2025 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/sc/debug_registry.h"
#include <string>
#include <utility>
#include <vector>
#include "ampere/sc/param.h"

namespace ampere::sc {

void DebugRegistry::add(const sc_core::sc_module &module, const char *category)
{
    auto mode = (std::strcmp(category, "*") == 0) ? ALL : FILTERED;
    auto &module_mode = modules.try_emplace(module.name()).first->second;
    if (module_mode < mode)
        module_mode = mode;
    categories.emplace(key(module.name(), category), true);
}

bool DebugRegistry::within_enabled_range(const Clock *clock) const
{
    if (clock && (start || stop)) {
        auto cycle = clock->get_cycle();
        if (start && cycle < *start)
            return false;
        if (stop && cycle > *stop)
            return false;
    }
    return true;
}

bool DebugRegistry::enabled(const sc_core::sc_module &module, const Clock *clock) const
{
    if (!within_enabled_range(clock))
        return false;

    if (all_enabled)
        return true;

    if (auto it = modules.find(module.name()); it != modules.end())
        return it->second == ALL;

    bool module_enabled = false;
    if (const auto *parent = dynamic_cast<Module *>(module.get_parent_object()); parent) {
        module_enabled = enabled(*parent);
    }
    modules.emplace(module.name(), module_enabled ? ALL : NONE);
    return module_enabled;
}

bool DebugRegistry::enabled(const sc_core::sc_module &module, const char *category, const Clock *clock) const
{
    if (enabled(module))
        return true;

    if (!within_enabled_range(clock))
        return false;

    auto category_key = key(module.name(), category);
    if (auto it = categories.find(category_key); it != categories.end())
        return it->second;

    bool category_enabled = false;
    if (const auto *parent = dynamic_cast<Module *>(module.get_parent_object()); parent) {
        category_enabled = enabled(*parent, category);
    }
    categories.emplace(category_key, category_enabled);
    return category_enabled;
}

void DebugRegistry::setup_main(Environment &env)
{
#ifndef NDEBUG
    auto &registry = env.debug;
    Param<bool> debug(env, "debug", "Enable all debug messages", false);
    if (*debug) {
        registry.set_enable_all();
        sc_core::sc_report_handler::set_verbosity_level(sc_core::SC_DEBUG);
    }
#endif
}

void DebugRegistry::setup_main_with_start_stop(Environment &env)
{
#ifndef NDEBUG
    setup_main(env);

    auto &registry = env.debug;
    Param<std::optional<Cycle>> debug_start(env, "debug_start", "Cycle to start debug", {});
    Param<std::optional<Cycle>> debug_stop(env, "debug_start", "Cycle to stop debug", {});

    if (debug_start)
        registry.set_enabled_at(*debug_start);
    if (debug_stop)
        registry.set_disabled_at(*debug_start);
#endif
}

void DebugRegistry::setup_module(const Module &m)
{
#ifndef NDEBUG
    auto &registry = m.get_environment()->debug;
    Param<std::vector<std::string>> debug("debug", "Enabled debug categories at hierarchy", std::vector<std::string>{});
    for (const auto &category : *debug)
        registry.add(m, category.c_str());

    if (!debug->empty())
        sc_core::sc_report_handler::set_verbosity_level(sc_core::SC_DEBUG);
#endif
}

} // namespace ampere::sc

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
