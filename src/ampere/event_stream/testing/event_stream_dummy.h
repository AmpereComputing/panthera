//
// Copyright (c) 2018-2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include <map>

#include "ampere/event_stream/event_stream.h"

namespace ampere::event_stream {

struct EventAnnouncer
{
    virtual ~EventAnnouncer() = default;
    virtual void post_event(const std::string& event_name, std::uint64_t time) = 0;
    virtual void add_int_data(const std::string& event_name,
                              int id,
                              const std::string& data_type_name,
                              std::int64_t value,
                              std::uint64_t time) = 0;
    virtual void add_uint_data(const std::string& event_name,
                               int id,
                               const std::string& data_type_name,
                               std::uint64_t value) = 0;
    virtual void add_string_data(const std::string& event_name,
                                 int id,
                                 const std::string& data_type_name,
                                 const std::string& value) = 0;
};

struct EventHandleDummy : public EventHandleBase
{
    explicit EventHandleDummy(EventType event_type, int id, std::uint64_t time)
        : event_type(event_type), id(id), time(time)
    {
    }
    EventType event_type;
    int id;
    std::uint64_t time;
};

class EventStreamDummy : public EventStream
{
public:
    EventStreamDummy(EventAnnouncer* announcer) : announcer(announcer) {}

    void post_event(EventType event_type, std::uint64_t time) final;
    EventHandle open_event(EventType event_type, std::uint64_t time) final;
    void close_event(EventHandle event_handle) final;

    void add_int_data(EventHandle event, DataType event_data_type, std::int64_t value) final;
    void add_uint_data(EventHandle event, DataType event_data_type, std::uint64_t value) final;
    void add_string_data(EventHandle event, DataType event_data_type, const std::string& value) final;

    void set_bool_parameter(const std::string& name, const std::string& description, bool value) final;
    void set_int_parameter(const std::string& name, const std::string& description, std::int64_t value) final;
    void set_uint_parameter(const std::string& name, const std::string& description, std::uint64_t value) final;
    void set_double_parameter(const std::string& name, const std::string& description, double value) final;
    void set_string_parameter(const std::string& name, const std::string& description, const std::string& value) final;
    void set_json_parameter(const std::string& name, const std::string& description, const std::string& value) final;

    void reset();
    void enable() final { enabled = true; }
    void disable() final { enabled = false; }

    std::uint64_t get_event_count(const std::string& event_name) const;

    bool get_bool_parameter(const std::string& event_name) const;
    std::uint64_t get_uint_parameter(const std::string& event_name) const;
    std::int64_t get_int_parameter(const std::string& event_name) const;
    double get_double_parameter(const std::string& event_name) const;
    std::string get_string_parameter(const std::string& event_name) const;
    std::string get_json_parameter(const std::string& event_name) const;

    std::string get_enum_string_value(const std::string& data_name, int64_t value) const;

private:
    std::map<std::string, EventType> event_name_to_type;
    std::map<EventType, std::string> event_type_to_name;

    std::map<std::string, DataType> data_name_to_type;
    std::map<DataType, std::string> data_type_to_name;
    std::map<DataType, EnumType> data_type_to_enum;
    std::map<EventType, std::uint64_t> counts;
    std::map<DataType, std::uint64_t> data_counts;

    std::map<std::string, bool> bool_param_values;
    std::map<std::string, std::int64_t> int_param_values;
    std::map<std::string, std::uint64_t> uint_param_values;
    std::map<std::string, double> double_param_values;
    std::map<std::string, std::string> string_param_values;
    std::map<std::string, std::string> json_param_values;

    std::map<EnumType, EnumMappingType> enumerations;

    int event_id{0};
    bool enabled{false};

    EventAnnouncer* announcer{nullptr};

    void finalize_event_definition(EventType event_type,
                                   const std::string& name,
                                   const std::string& description) override;
    void finalize_data_definition(DataType data_type, const std::string& name, const std::string& description) override;
    void finalize_data_definition(DataType data_type,
                                  const std::string& name,
                                  const std::string& description,
                                  EnumType enumeration) override;
    void finalize_enum_definition(EnumType enum_type, const EnumMappingType& enumerations) override;
};

} // namespace ampere::event_stream

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
