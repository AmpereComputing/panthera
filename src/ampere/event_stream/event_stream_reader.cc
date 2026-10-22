//
// Copyright (c) 2018-2019, 2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "event_stream_reader.h"

namespace ampere::event_stream {

EventStreamReader::EventStreamReader(int fd) : ProtobufStreamReader(fd, protobuf_magic, protobuf_es_version) {}

EventStreamReader::EventStreamReader(boost::filesystem::path filepath)
    : ProtobufStreamReader(filepath, protobuf_magic, protobuf_es_version)
{
}

bool EventStreamReader::read(event_stream_proto::Record& record)
{
    bool read_record = ProtobufStreamReader::read(record);
    if (read_record && record.has_definition())
        store_definition(record);

    return read_record;
}

std::optional<event_stream_proto::Record> EventStreamReader::read()
{
    event_stream_proto::Record record;
    if (read(record))
        return record;
    return std::nullopt;
}

std::optional<event_stream_proto::Event> EventStreamReader::read_event()
{
    event_stream_proto::Record record;
    while (ProtobufStreamReader::read(record)) {
        if (record.has_definition()) {
            store_definition(record);
        } else if (record.has_event()) {
            const auto& event = record.event();
            auto definition = definitions[event.definition_id()];
            if (definition.enable)
                return event;
        } else if (record.has_enumeration()) {
            enumerations[record.enumeration().id()] = record.enumeration();
        }
        record.Clear();
    }
    return std::nullopt;
}

const char* EventStreamReader::enumeration_value(const event_stream_proto::Enumeration& enumerations, int64_t value)
{
    if (auto name = enumerations.values().find(value); name != enumerations.values().end())
        return name->second.c_str();
    return nullptr;
}

void EventStreamReader::include(std::string event_name)
{
    events_to_include.insert(event_name);
}

void EventStreamReader::store_definition(const event_stream_proto::Record& record)
{
    auto name = record.definition().name();
    DefinitionEntry entry;
    entry.definition = record.definition();
    entry.enable = events_to_include.empty() || events_to_include.contains(name);
    definitions[record.definition().id()] = entry;
}

std::list<event_stream_proto::Definition> EventStreamReader::all_definitions() const
{
    std::list<event_stream_proto::Definition> defs;
    for (const auto& i : definitions) {
        defs.push_back(i.second.definition);
    }
    return defs;
}

} // namespace ampere::event_stream

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
