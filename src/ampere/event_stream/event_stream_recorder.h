//
// Copyright (c) 2018-2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <memory>
#include <unordered_set>
#include <variant>
#include <map>
#include <vector>
#include <boost/pool/object_pool.hpp>

#include "ampere/event_stream/event_stream.h"

namespace ampere::event_stream {

struct RecorderEventHandle : EventHandleBase
{
    EventType event{};
    std::uint64_t time;
    std::map<DataType, std::variant<int64_t, uint64_t, std::string>> data;

    RecorderEventHandle() : time(std::numeric_limits<uint64_t>::max()) {}
    RecorderEventHandle(EventType event, std::uint64_t time) : event(event), time(time) {}

    explicit operator bool() const { return time != std::numeric_limits<uint64_t>::max(); }
};

class EventStreamRecorder : public EventStream
{
public:
    class RecordedEvent : public RecorderEventHandle
    {
        EventStreamRecorder const* es;

    public:
        RecordedEvent(const RecorderEventHandle& handle, const EventStreamRecorder* es)
            : RecorderEventHandle(handle), es(es)
        {
        }

        auto name() const { return es->definitions->get_definition(event).name; }
        auto data_name(DataType id) const { return es->definitions->get_definition(id).name; }
    };

    using callback_func_t = std::function<void(const RecordedEvent&)>;

private:
    std::deque<RecordedEvent> events;

    callback_func_t callback_func;
    std::unordered_set<std::string> events_to_listen;
    std::unordered_set<EventType> listened_events;

    bool listening(EventType event) const { return listened_events.find(event) != listened_events.end(); }
    void record_event(const RecorderEventHandle& event);

public:
    void record(const std::string& event_name) { events_to_listen.emplace(event_name); }
    void callback(const callback_func_t& func) { callback_func = func; }

    void post_event(EventType event_type, std::uint64_t time) override;
    EventHandle open_event(EventType event_type, std::uint64_t time) override;
    void close_event(EventHandle event) override;

    void add_int_data(EventHandle event, DataType event_data_type, std::int64_t value) override;
    void add_uint_data(EventHandle event, DataType event_data_type, std::uint64_t value) override;
    void add_string_data(EventHandle event, DataType event_data_type, const std::string& value) override;

    void set_bool_parameter(const std::string& name, const std::string& description, bool value) override {}
    void set_int_parameter(const std::string& name, const std::string& description, std::int64_t value) override {}
    void set_uint_parameter(const std::string& name, const std::string& description, std::uint64_t value) override {}
    void set_double_parameter(const std::string& name, const std::string& description, double value) override {}
    void set_string_parameter(const std::string& name,
                              const std::string& description,
                              const std::string& value) override
    {
    }
    void set_json_parameter(const std::string& name, const std::string& description, const std::string& value) override
    {
    }

    void start_simulation() override { catch_up(); }
    void enable() override { enabled = true; }
    void disable() override { enabled = false; }
    bool is_enabled() override { return enabled; }

    bool has_event() const { return !events.empty(); }
    auto pop_event()
    {
        auto event = std::move(events.front());
        events.pop_front();
        return event;
    }

protected:
    void finalize_event_definition(EventType event_type,
                                   const std::string& name,
                                   const std::string& description) override
    {
        if (events_to_listen.find(name) != events_to_listen.end())
            listened_events.emplace(event_type);
    }
    void finalize_data_definition(DataType data_type, const std::string& name, const std::string& description) override
    {
    }
    void finalize_data_definition(DataType data_type,
                                  const std::string& name,
                                  const std::string& description,
                                  EnumType enumeration [[maybe_unused]]) override
    {
    }
    void finalize_enum_definition(EnumType enum_type [[maybe_unused]],
                                  const EnumMappingType& enumerations [[maybe_unused]]) override
    {
    }

private:
    bool enabled{true};
    boost::object_pool<RecorderEventHandle> event_handle_pool{32, 0};
};

} // namespace ampere::event_stream

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
