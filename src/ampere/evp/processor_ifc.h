//
// Copyright(c) 2022 Ampere Computing LLC
// SPDX-License-Identifier: BSD-3-Clause
//
#pragma once

#include <functional>
#include <set>

#include "ampere/event_stream/event_stream.pb.h"

#include "counter.h"

namespace ampere::evp {

using event_stream_proto::Definition;
using event_stream_proto::Enumeration;
using event_stream_proto::Event;
using event_stream_proto::Parameter;
using event_stream_proto::Record;

using CounterSet = std::set<int>;
using TimeAction = std::function<void(uint64_t current_time, uint64_t expiry)>;

class Plugin;

enum class CountAction {
    ADD,
    REMOVE
};

/** Abstract interface implemented by the Processor.
 *
 *  This is how plugins call back into the processor to schedule things,
 *  obtain definitions from the event stream, or access counters they
 *  have installed.
 */
struct ProcessorIfc
{
    virtual ~ProcessorIfc() = default;

    virtual bool has_definition(int id) const = 0;
    virtual bool has_definition(const std::string& name) const = 0;
    virtual bool has_parameter(const std::string& name) const = 0;
    virtual bool has_enumeration(int id) const = 0;

    virtual const Definition& get_definition(int id) const = 0;
    virtual const Definition& get_definition(const std::string& name) const = 0;
    virtual const Parameter& get_parameter(const std::string& name) const = 0;
    virtual const Enumeration& get_enumeration(int id) const = 0;

    virtual uint64_t get_current_time() const = 0;
    virtual uint64_t get_first_event_time() const = 0;

    virtual Counter& get_counter(int counter_id) = 0;

    virtual void count(Plugin* plugin,
                       const std::string& counter_spec,
                       CounterSet* counter_set = nullptr,
                       CountAction action = CountAction::ADD) = 0;
    virtual void on(Plugin* plugin, const std::string& trigger, Action action, CounterSet* counter_set = nullptr) = 0;
    virtual void on_every(Plugin* plugin,
                          const std::string& trigger,
                          Action action,
                          CounterSet* counter_set = nullptr) = 0;
    virtual void at(Plugin* plugin, const std::string& trigger, TimeAction action) = 0;
    virtual void at_every(Plugin* plugin, const std::string& trigger, TimeAction action) = 0;
    virtual void accumulate(Plugin* plugin, const std::string& counter_spec, CounterSet* counter_set = nullptr) = 0;
    virtual void stop(const std::string& msg) = 0;

    virtual const std::map<std::string, std::string>& vars() = 0;
    virtual fmt::format_args vars_for_fmt() = 0;

    bool has_enumeration(const Definition& definition) const
    {
        return definition.has_enumeration_id() && has_enumeration(definition.enumeration_id());
    }
    const char* enumeration_value(int definition_id, int64_t value) const;
    const char* enumeration_value_for_definition(const Definition& definition, int64_t value) const;
};

struct NoSuchDefinition : public std::runtime_error
{
    NoSuchDefinition(const std::string& name)
        : std::runtime_error{fmt::format("no definition for event or data item \"{}\"", name)}
    {
    }
};

} // namespace ampere::evp
