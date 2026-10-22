//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#pragma once

#include <systemc>

#include "ampere/event_stream/event_stream.h"
#include "ampere/sc/param_registry.h"
#ifndef NDEBUG
#include "ampere/sc/debug_registry.h"
#endif

namespace ampere::sc {

class Environment : sc_core::sc_module
{
public:
    explicit Environment(const sc_core::sc_module_name& name);
    Environment(const sc_core::sc_module_name& name, std::initializer_list<std::pair<std::string, json>> params);

    static Environment* get_root();
    static void set_root(std::unique_ptr<Environment> env);

    std::unique_ptr<event_stream::EventStream> event_stream;
    ParameterRegistry parameters;
#ifndef NDEBUG
    DebugRegistry debug;
#endif

private:
    static std::unique_ptr<Environment> root_env;
    void start_of_simulation() override;
};

} // namespace ampere::sc

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
