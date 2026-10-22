//
// Copyright (c) 2018-2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/event_stream/testing/event_stream_dummy.h"

namespace ampere::event_stream {

void EventStreamDummy::post_event(EventType event_type, std::uint64_t time)
{
    catch_up();
    if (announcer)
        announcer->post_event(event_type_to_name.at(event_type), time);
    if (enabled)
        counts[event_type]++;
    event_id++;
}

EventHandle EventStreamDummy::open_event(EventType event_type, std::uint64_t time)
{
    catch_up();
    if (announcer)
        announcer->post_event(event_type_to_name.at(event_type), time);
    if (enabled)
        counts[event_type]++;
    return new EventHandleDummy{event_type, event_id++, time};
}

void EventStreamDummy::close_event(EventHandle event_handle)
{
    delete event_handle;
}

void EventStreamDummy::add_int_data(EventHandle event, DataType event_data_type, std::int64_t value)
{
    if (announcer) {
        auto eh = dynamic_cast<EventHandleDummy*>(event);
        announcer->add_int_data(
            event_type_to_name.at(eh->event_type), eh->id, data_type_to_name[event_data_type], value, eh->time);
    }
}

void EventStreamDummy::add_uint_data(EventHandle event, DataType event_data_type, std::uint64_t value)
{
    if (announcer) {
        auto eh = dynamic_cast<EventHandleDummy*>(event);
        announcer->add_uint_data(
            event_type_to_name.at(eh->event_type), eh->id, data_type_to_name[event_data_type], value);
    }
}

void EventStreamDummy::add_string_data(EventHandle event, DataType event_data_type, const std::string& value)
{
    if (announcer) {
        auto eh = dynamic_cast<EventHandleDummy*>(event);
        announcer->add_string_data(
            event_type_to_name.at(eh->event_type), eh->id, data_type_to_name[event_data_type], value);
    }
}

void EventStreamDummy::set_bool_parameter(const std::string& name, const std::string& description, bool value)
{
    bool_param_values[name] = value;
}

void EventStreamDummy::set_int_parameter(const std::string& name, const std::string& description, std::int64_t value)
{
    int_param_values[name] = value;
}

void EventStreamDummy::set_uint_parameter(const std::string& name, const std::string& description, std::uint64_t value)
{
    uint_param_values[name] = value;
}

void EventStreamDummy::set_double_parameter(const std::string& name, const std::string& description, double value)
{
    double_param_values[name] = value;
}

void EventStreamDummy::set_string_parameter(const std::string& name,
                                            const std::string& description,
                                            const std::string& value)
{
    string_param_values[name] = value;
}

void EventStreamDummy::set_json_parameter(const std::string& name,
                                          const std::string& description,
                                          const std::string& value)
{
    json_param_values[name] = value;
}

bool EventStreamDummy::get_bool_parameter(const std::string& event_name) const
{
    return bool_param_values.at(event_name);
}

std::int64_t EventStreamDummy::get_int_parameter(const std::string& event_name) const
{
    return int_param_values.at(event_name);
}

std::uint64_t EventStreamDummy::get_uint_parameter(const std::string& event_name) const
{
    return uint_param_values.at(event_name);
}

double EventStreamDummy::get_double_parameter(const std::string& event_name) const
{
    return double_param_values.at(event_name);
}

std::string EventStreamDummy::get_string_parameter(const std::string& event_name) const
{
    return string_param_values.at(event_name);
}

void EventStreamDummy::finalize_event_definition(EventType event_type,
                                                 const std::string& name,
                                                 const std::string& description)
{
    event_name_to_type[name] = event_type;
    event_type_to_name[event_type] = name;
}

void EventStreamDummy::finalize_data_definition(DataType data_type,
                                                const std::string& name,
                                                const std::string& description)
{
    data_name_to_type[name] = data_type;
    data_type_to_name[data_type] = name;
}

void EventStreamDummy::finalize_data_definition(DataType data_type,
                                                const std::string& name,
                                                const std::string& description,
                                                EnumType enumeration)
{
    finalize_data_definition(data_type, name, description);
    data_type_to_enum[data_type] = enumeration;
}

void EventStreamDummy::finalize_enum_definition(EnumType enum_type, const EnumMappingType& enumerations)
{
    this->enumerations[enum_type] = enumerations;
}

void EventStreamDummy::reset()
{
    event_name_to_type.clear();
    event_type_to_name.clear();
    data_name_to_type.clear();
    data_type_to_name.clear();
    data_type_to_enum.clear();
    counts.clear();
    data_counts.clear();
    bool_param_values.clear();
    int_param_values.clear();
    uint_param_values.clear();
    double_param_values.clear();
    string_param_values.clear();
    json_param_values.clear();
    enumerations.clear();

    event_id = 0;
}

std::uint64_t EventStreamDummy::get_event_count(const std::string& event_name) const
{
    if (auto event_type = event_name_to_type.find(event_name); event_type != event_name_to_type.end())
        if (auto c = counts.find(event_type->second); c != counts.end())
            return c->second;
    return 0;
}

std::string EventStreamDummy::get_enum_string_value(const std::string& data_name, int64_t value) const
{
    auto data_type = data_name_to_type.at(data_name);
    if (auto enum_type = data_type_to_enum.find(data_type); enum_type != data_type_to_enum.end())
        return enumerations.at(enum_type->second).at(value);
    return {};
}

} // namespace ampere::event_stream

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
