//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include "environment.h"

#include <boost/make_unique.hpp>
#include <iostream>

#include "ampere/event_stream/event_stream_proto.h"
#include "ampere/event_stream/event_stream_sink.h"
#include "ampere/sc/environment.h"

namespace ampere::sc {

Environment::Environment(const sc_core::sc_module_name& name) : Environment(name, {}) {}

Environment::Environment(const sc_core::sc_module_name& name,
                         std::initializer_list<std::pair<std::string, json>> params)
    : sc_core::sc_module{name}, event_stream(new event_stream::EventStreamSink{}), parameters(params)
{
}

void Environment::start_of_simulation()
{
    event_stream->start_simulation();
}

std::unique_ptr<Environment> Environment::root_env = nullptr;

Environment* Environment::get_root()
{
    if (!root_env)
        root_env = std::make_unique<Environment>("system");

    return root_env.get();
}

void Environment::set_root(std::unique_ptr<Environment> env)
{
    root_env = std::move(env);
}

} // namespace ampere::sc

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
