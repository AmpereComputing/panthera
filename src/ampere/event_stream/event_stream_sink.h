//
// Copyright (c) 2018-2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#ifndef AMPERE_EVENT_STREAM_SINK_H
#define AMPERE_EVENT_STREAM_SINK_H

#include "ampere/event_stream/event_stream.h"

namespace ampere::event_stream {

class EventStreamSink : public EventStream
{
public:
    void post_event(EventType event_type, std::uint64_t time) final {}
    EventHandle open_event(EventType event_type, std::uint64_t time) final { return &sink_handle; }
    void close_event(EventHandle event_handle) final {}
    void add_int_data(EventHandle event, DataType event_data_type, std::int64_t value) final {}
    void add_uint_data(EventHandle event, DataType event_data_type, std::uint64_t value) final {}
    void add_string_data(EventHandle event, DataType event_data_type, const std::string& value) final {}
    void set_bool_parameter(const std::string& name, const std::string& description, bool value) final {};
    void set_int_parameter(const std::string& name, const std::string& description, std::int64_t value) final {};
    void set_uint_parameter(const std::string& name, const std::string& description, std::uint64_t value) final {};
    void set_double_parameter(const std::string& name, const std::string& description, double value) final {};
    void set_string_parameter(const std::string& name, const std::string& description, const std::string& value) final {
    };
    void set_json_parameter(const std::string& name, const std::string& description, const std::string& value) final {};
    void enable() final {}
    void disable() final {}

private:
    void finalize_event_definition(EventType event_type, const std::string& name, const std::string& description) final
    {
    }
    void finalize_data_definition(DataType data_type, const std::string& name, const std::string& description) final {}

    static EventHandleBase sink_handle;
};

} // namespace ampere::event_stream

#endif //  AMPERE_EVENT_STREAM_SINK_H

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
