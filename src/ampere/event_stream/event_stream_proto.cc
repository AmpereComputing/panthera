//
// Copyright (c) 2018-2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include "event_stream_proto.h"

#include <fcntl.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>
#include <boost/smart_ptr/make_unique.hpp>
#include <system_error>

#include "ampere/event_stream/event_stream.h"
#include "ampere/event_stream/event_stream.pb.h"
#include "ampere/protobuf_utils/protobuf_utils.h"

namespace ampere::event_stream {

EventStreamProto::EventStreamProto(int fd)
{
    ev_writer = std::make_unique<ProtobufStreamWriter>(fd, protobuf_magic, protobuf_es_version);
}

EventStreamProto::EventStreamProto(int fd_writer, int fd_reader)
{
    ev_writer = std::make_unique<ProtobufStreamWriter>(fd_writer, protobuf_magic, protobuf_es_version);
    ev_reader = std::make_unique<ProtobufStreamReader>(fd_reader, protobuf_magic, protobuf_es_version);
}

int EventStreamProto::open_file(const std::string& filename)
{
    int fd = ::open(filename.c_str(), O_CREAT | O_RDWR | O_TRUNC, S_IWUSR | S_IRUSR | S_IRGRP | S_IROTH);

    if (fd == -1)
        throw std::system_error(errno, std::system_category(), filename);

    return fd;
}

void EventStreamProto::finalize_event_definition(EventType event_type,
                                                 const std::string& name,
                                                 const std::string& description)
{
    // Write event definition and we always expect a read
    event_stream_proto::Definition definition;

    definition.set_name(name);
    definition.set_description(description);
    definition.set_id(event_type);
    definition.set_kind(event_stream_proto::EVENT);

    write(definition);
    ++num_events;
}

void EventStreamProto::finalize_data_definition(DataType data_type,
                                                const std::string& name,
                                                const std::string& description)
{
    event_stream_proto::Definition definition;

    definition.set_name(name);
    definition.set_description(description);
    definition.set_id(data_type);
    definition.set_kind(event_stream_proto::VALUE);

    write(definition);
}

void EventStreamProto::finalize_data_definition(DataType data_type,
                                                const std::string& name,
                                                const std::string& description,
                                                EnumType enumeration)
{
    event_stream_proto::Definition definition;

    definition.set_name(name);
    definition.set_description(description);
    definition.set_id(data_type);
    definition.set_kind(event_stream_proto::VALUE);
    definition.set_enumeration_id(enumeration);

    write(definition);
}

void EventStreamProto::finalize_enum_definition(EnumType enum_type, const EnumMappingType& enumerations)
{
    event_stream_proto::Enumeration enumeration;

    enumeration.set_id(enum_type);
    enumeration.mutable_values()->insert(enumerations.begin(), enumerations.end());

    write(enumeration);
}

void EventStreamProto::set_bool_parameter(const std::string& name, const std::string& description, bool value)
{
    event_stream_proto::Parameter parameter;

    parameter.set_name(name);
    parameter.set_description(description);
    parameter.set_bool_value(value);

    write(parameter);
}

void EventStreamProto::set_int_parameter(const std::string& name, const std::string& description, std::int64_t value)
{
    event_stream_proto::Parameter parameter;

    parameter.set_name(name);
    parameter.set_description(description);
    parameter.set_int_value(value);

    write(parameter);
}

void EventStreamProto::set_uint_parameter(const std::string& name, const std::string& description, std::uint64_t value)
{
    event_stream_proto::Parameter parameter;

    parameter.set_name(name);
    parameter.set_description(description);
    parameter.set_uint_value(value);

    write(parameter);
}

void EventStreamProto::set_double_parameter(const std::string& name, const std::string& description, double value)
{
    event_stream_proto::Parameter parameter;

    parameter.set_name(name);
    parameter.set_description(description);
    parameter.set_double_value(value);

    write(parameter);
}

void EventStreamProto::set_string_parameter(const std::string& name,
                                            const std::string& description,
                                            const std::string& value)
{
    event_stream_proto::Parameter parameter;

    parameter.set_name(name);
    parameter.set_description(description);
    parameter.set_string_value(value);

    write(parameter);
}

void EventStreamProto::set_json_parameter(const std::string& name,
                                          const std::string& description,
                                          const std::string& value)
{
    event_stream_proto::Parameter parameter;

    parameter.set_name(name);
    parameter.set_description(description);
    parameter.set_json_value(value);

    write(parameter);
}

void EventStreamProto::post_event(EventType event_type, std::uint64_t time)
{
    catch_up();

    auto id = current_event_id++;
    event_stream_proto::Event event_proto;

    event_proto.set_id(id);
    event_proto.set_definition_id(event_type);
    event_proto.set_time(time);

    write(event_proto);
}

EventHandle EventStreamProto::open_event(EventType event_type, std::uint64_t time)
{
    catch_up();

    auto id = current_event_id++;
    auto eh = event_handle_pool.construct();

    eh->event_proto.set_id(id);
    eh->event_proto.set_definition_id(event_type);
    eh->event_proto.set_time(time);

    return eh;
}

void EventStreamProto::close_event(EventHandle event)
{
    auto eh = static_cast<EventHandleProto*>(event);
    write(eh->event_proto);
    event_handle_pool.destroy(eh);
}

void EventStreamProto::add_int_data(EventHandle event, DataType data_type, std::int64_t value)
{
    auto eh = static_cast<EventHandleProto*>(event);
    auto new_value = eh->event_proto.add_values();
    new_value->set_definition_id(data_type);
    new_value->set_int_value(value);
}

void EventStreamProto::add_uint_data(EventHandle event, DataType data_type, std::uint64_t value)
{
    auto eh = static_cast<EventHandleProto*>(event);
    auto new_value = eh->event_proto.add_values();
    new_value->set_definition_id(data_type);
    new_value->set_uint_value(value);
}

void EventStreamProto::add_string_data(EventHandle event, DataType data_type, const std::string& value)
{
    auto eh = static_cast<EventHandleProto*>(event);
    auto new_value = eh->event_proto.add_values();
    new_value->set_definition_id(data_type);
    new_value->set_string_value(value);
}

void EventStreamProto::start_simulation()
{
    catch_up();

    event_stream_proto::Control control;
    control.set_type(event_stream_proto::START_SIMULATION);
    this->write(control);
    ev_writer->flush();

    if (ev_reader) {
        event_stream_proto::Response response;

        // We should get a response back for every event definition we sent.
        for (int i = 0; i < num_events; i++) {
            ev_reader->read(response);

            int id = response.definition().id();
            bool enabled = response.definition().enable();

            auto event = Definitions::instance().get_event(id);

            if (event) {
                if (enabled)
                    event->enable();
                else
                    event->disable();
            }

            response.Clear();
        }
    }
}

void EventStreamProto::write(event_stream_proto::Definition& definition)
{
    event_stream_proto::Record record;
    record.set_allocated_definition(&definition);
    write(record);
    static_cast<void>(record.release_definition());
}

void EventStreamProto::write(event_stream_proto::Definition& definition, int id, bool& enabled)
{
    event_stream_proto::Record record;
    event_stream_proto::Response response;
    record.set_allocated_definition(&definition);
    write_read(record, response);
    enabled = response.definition().enable();
    id = response.definition().id();
    static_cast<void>(record.release_definition());
    static_cast<void>(response.release_definition());
}

void EventStreamProto::write(event_stream_proto::Control& control)
{
    event_stream_proto::Record record;
    record.set_allocated_control(&control);
    this->write(record);
    static_cast<void>(record.release_control());
}

void EventStreamProto::write(event_stream_proto::Event& event)
{
    if (enabled) {
        event_stream_proto::Record record;
        record.set_allocated_event(&event);
        write(record);
        static_cast<void>(record.release_event());
    }
}

void EventStreamProto::write(event_stream_proto::Parameter& parameter)
{
    if (enabled) {
        event_stream_proto::Record record;
        record.set_allocated_parameter(&parameter);
        write(record);
        static_cast<void>(record.release_parameter());
    }
}

void EventStreamProto::write(event_stream_proto::Enumeration& enumeration)
{
    event_stream_proto::Record record;
    record.set_allocated_enumeration(&enumeration);
    write(record);
    static_cast<void>(record.release_enumeration());
}

void EventStreamProto::write(event_stream_proto::Record& record)
{
    std::unique_lock<std::mutex> lock{output_mtx};
    ev_writer->write(record);
}

void EventStreamProto::write_read(event_stream_proto::Record& record, event_stream_proto::Response& response)
{
    std::unique_lock<std::mutex> lock{output_mtx};
    ev_writer->write(record);
    ev_writer->flush();
    if (ev_reader)
        ev_reader->read(response);
}

} // namespace ampere::event_stream

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
