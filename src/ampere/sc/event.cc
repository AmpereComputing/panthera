//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include "ampere/sc/event.h"
#include "ampere/sc/ancestor.h"
#include "ampere/sc/module.h"
#include "ampere/sc/visit.h"
#include "event.h"

namespace ampere::sc {

namespace internal {

event_stream::EventStream* EventDataBase::get_event_stream()
{
    if (auto* mod = get_nearest_ancestor<Module>(this); mod && mod->get_environment()->event_stream)
        return mod->get_environment()->event_stream.get();
    return nullptr;
}

} // namespace internal

Event::Event(const std::string& name, const std::string& description)
    : sc_core::sc_object(name.c_str()),
      EventBase(sc_object::name(), description),
      event_stream(*get_nearest_ancestor<Module>(this)->get_environment()->event_stream),
      definition(event_stream.define_event(this)),
      enabled(true)
{
}

Event::Event(event_stream::EventStream& event_stream, const std::string& name, const std::string& description)
    : sc_core::sc_object(name.c_str()),
      EventBase(sc_object::name(), description),
      event_stream(event_stream),
      definition(event_stream.define_event(this)),
      enabled(true)
{
}

void Event::enable_all()
{
    std::function<void(Event*)> enable_event = [](Event* e) {
        e->enable();
    };
    visit_objects("ampere::sc::Event", enable_event);
}

void Event::disable_all()
{
    std::function<void(Event*)> disable_event = [](Event* e) {
        e->disable();
    };
    visit_objects("ampere::sc::Event", disable_event);
}

} // namespace ampere::sc

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
