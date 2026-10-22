//
// Copyright (c) 2018-2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#pragma once

#include <algorithm>
#include <cstddef>
#include <memory>
#include <vector>
#include <boost/pool/object_pool.hpp>

#include "ampere/event_stream/event_stream.h"

namespace ampere::event_stream {

struct MultipleEventHandle : EventHandleBase
{
    std::vector<EventHandle> handles;
};

class EventStreamBroadcast : public EventStream
{
    std::vector<std::unique_ptr<EventStream>> event_streams;

    template<typename P, typename... Args>
    void broadcast(P func, Args&&... args)
    {
        for (auto& es : event_streams)
            ((*es).*func)(std::forward<Args>(args)...);
    }

    template<typename P, typename... Args>
    void broadcast_with_handle(P func, MultipleEventHandle* handle, Args&&... args)
    {
        for (size_t i = 0; i < event_streams.size(); ++i)
            ((*event_streams[i]).*func)(handle->handles[i], std::forward<Args>(args)...);
    }

public:
    auto* add(std::unique_ptr<EventStream>&& es)
    {
        auto& e = event_streams.emplace_back(std::move(es));
        return e.get();
    }
    template<typename T, typename... Args>
    T* emplace(Args&&... args)
    {
        auto& e = event_streams.emplace_back(std::make_unique<T>(std::forward<Args>(args)...));
        return static_cast<T*>(e.get());
    }

    void post_event(EventType event_type, std::uint64_t time) override
    {
        broadcast(&EventStream::post_event, event_type, time);
    }
    EventHandle open_event(EventType event_type, std::uint64_t time) override
    {
        auto* handle = event_handle_pool.construct();
        for (auto& es : event_streams)
            handle->handles.emplace_back(es->open_event(event_type, time));

        return handle;
    }
    void close_event(EventHandle event) override
    {
        auto* handle = static_cast<MultipleEventHandle*>(event);
        broadcast_with_handle(&EventStream::close_event, handle);
        event_handle_pool.destroy(handle);
    }

    void add_int_data(EventHandle event, DataType event_data_type, std::int64_t value) override
    {
        auto* handle = static_cast<MultipleEventHandle*>(event);
        broadcast_with_handle(&EventStream::add_int_data, handle, event_data_type, value);
    }
    void add_uint_data(EventHandle event, DataType event_data_type, std::uint64_t value) override
    {
        auto* handle = static_cast<MultipleEventHandle*>(event);
        broadcast_with_handle(&EventStream::add_uint_data, handle, event_data_type, value);
    }
    void add_string_data(EventHandle event, DataType event_data_type, const std::string& value) override
    {
        auto* handle = static_cast<MultipleEventHandle*>(event);
        broadcast_with_handle(&EventStream::add_string_data, handle, event_data_type, value);
    }

    void set_bool_parameter(const std::string& name, const std::string& description, bool value) override
    {
        broadcast(&EventStream::set_bool_parameter, name, description, value);
    }
    void set_int_parameter(const std::string& name, const std::string& description, std::int64_t value) override
    {
        broadcast(&EventStream::set_int_parameter, name, description, value);
    }
    void set_uint_parameter(const std::string& name, const std::string& description, std::uint64_t value) override
    {
        broadcast(&EventStream::set_uint_parameter, name, description, value);
    }
    void set_double_parameter(const std::string& name, const std::string& description, double value) override
    {
        broadcast(&EventStream::set_double_parameter, name, description, value);
    }
    void set_string_parameter(const std::string& name,
                              const std::string& description,
                              const std::string& value) override
    {
        broadcast(&EventStream::set_string_parameter, name, description, value);
    }
    void set_json_parameter(const std::string& name, const std::string& description, const std::string& value) override
    {
        broadcast(&EventStream::set_json_parameter, name, description, value);
    }

    void start_simulation() override { broadcast(&EventStream::start_simulation); }
    void enable() override { broadcast(&EventStream::enable); }
    void disable() override { broadcast(&EventStream::disable); }
    bool is_enabled() override
    {
        return std::any_of(event_streams.begin(), event_streams.end(), [](const auto& es) { return es->is_enabled(); });
    }

protected:
    void finalize_event_definition(EventType event_type,
                                   const std::string& name,
                                   const std::string& description) override
    {
        broadcast(&EventStream::finalize_event_definition, event_type, name, description);
    }
    void finalize_data_definition(DataType data_type, const std::string& name, const std::string& description) override
    {
        using data_func_t = void (EventStream::*)(DataType, const std::string&, const std::string&);
        broadcast(static_cast<data_func_t>(&EventStream::finalize_data_definition), data_type, name, description);
    }
    void finalize_data_definition(DataType data_type,
                                  const std::string& name,
                                  const std::string& description,
                                  EnumType enumeration [[maybe_unused]]) override
    {
        using data_func_t = void (EventStream::*)(DataType, const std::string&, const std::string&, EnumType);
        broadcast(static_cast<data_func_t>(&EventStream::finalize_data_definition),
                  data_type,
                  name,
                  description,
                  enumeration);
    }
    void finalize_enum_definition(EnumType enum_type [[maybe_unused]],
                                  const EnumMappingType& enumerations [[maybe_unused]]) override
    {
        broadcast(&EventStream::finalize_enum_definition, enum_type, enumerations);
    }

private:
    boost::object_pool<MultipleEventHandle> event_handle_pool{32, 0};
};

} // namespace ampere::event_stream

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
