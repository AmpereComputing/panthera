//
// Copyright (c) 2018-2019, 2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#ifndef AMPERE_EVENT_STREAM_READER_H
#define AMPERE_EVENT_STREAM_READER_H

#include <optional>
#include <list>
#include <unordered_map>
#include <unordered_set>

#include "ampere/event_stream/event_stream.pb.h"
#include "ampere/protobuf_utils/protobuf_stream.h"

#include "ampere/event_stream/event_stream_proto.h"

namespace ampere::event_stream {

class EventStreamReader : public ProtobufStreamReader
{
public:
    explicit EventStreamReader(int fd);
    explicit EventStreamReader(boost::filesystem::path filepath);
    void include(std::string event_name);

    bool read(event_stream_proto::Record& record);
    std::optional<event_stream_proto::Record> read();

    std::optional<event_stream_proto::Event> read_event();
    std::list<event_stream_proto::Definition> all_definitions() const;

    template<typename T>
    std::string name(const T& t) const
    {
        return definitions.at(t.definition_id()).definition.name();
    }

    const char* definition_name(int definition_id) const
    {
        return definitions.at(definition_id).definition.name().c_str();
    }

    static const char* enumeration_value(const event_stream_proto::Enumeration& enumerations, int64_t value);
    const char* enumeration_value_for_definition(int definition_id, int64_t value)
    {
        return enumeration_value_for_definition(definitions.at(definition_id).definition, value);
    }
    const char* enumeration_value_for_definition(const event_stream_proto::Definition& definition, int64_t value)
    {
        if (!definition.has_enumeration_id()) {
            if (auto values = enumerations.find(definition.enumeration_id()); values != enumerations.end())
                return enumeration_value(values->second, value);
        }

        return nullptr;
    }

private:
    void store_definition(const event_stream_proto::Record& record);
    struct DefinitionEntry
    {
        ampere::event_stream_proto::Definition definition;
        bool enable{true};
    };
    std::unordered_set<std::string> events_to_include;
    std::unordered_map<int, DefinitionEntry> definitions;
    std::unordered_map<int, ampere::event_stream_proto::Enumeration> enumerations;
};

} // namespace ampere::event_stream

#endif // AMPERE_EVENT_STREAM_READER_H

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
