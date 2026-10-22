//
// Copyright (c) 2018-2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include <gmock/gmock.h>
#include <memory>

#include "ampere/event_stream/event_stream.h"
#include "ampere/event_stream/event_stream_sink.h"
#include "ampere/event_stream/testing/event_stream_dummy.h"

namespace ampere::event_stream {

struct MockEventStream : public EventStream
{
    MockEventStream() = default;
    explicit MockEventStream(bool local_only)
    {
        if (local_only) {
            // Uses local definitions/enumerations instead of global
            defs = std::make_unique<Definitions>();
            definitions = defs.get();

            enums = std::make_unique<Enumerations>();
            enumerations = enums.get();
        }
    }

    MOCK_METHOD(void, post_event, (EventType event_type, std::uint64_t time), (override));
    MOCK_METHOD(EventHandle, open_event, (EventType event_type, std::uint64_t time), (override));
    MOCK_METHOD(void, close_event, (EventHandle event), (override));

    MOCK_METHOD(void, add_int_data, (EventHandle event, DataType event_data_type, std::int64_t value), (override));
    MOCK_METHOD(void, add_uint_data, (EventHandle event, DataType event_data_type, std::uint64_t value), (override));
    MOCK_METHOD(void,
                add_string_data,
                (EventHandle event, DataType event_data_type, const std::string& value),
                (override));

    MOCK_METHOD(void,
                set_bool_parameter,
                (const std::string& name, const std::string& description, bool value),
                (override));
    MOCK_METHOD(void,
                set_int_parameter,
                (const std::string& name, const std::string& description, std::int64_t value),
                (override));
    MOCK_METHOD(void,
                set_uint_parameter,
                (const std::string& name, const std::string& description, std::uint64_t value),
                (override));
    MOCK_METHOD(void,
                set_double_parameter,
                (const std::string& name, const std::string& description, double value),
                (override));
    MOCK_METHOD(void,
                set_string_parameter,
                (const std::string& name, const std::string& description, const std::string& value),
                (override));
    MOCK_METHOD(void,
                set_json_parameter,
                (const std::string& name, const std::string& description, const std::string& value),
                (override));

    MOCK_METHOD(void,
                finalize_event_definition,
                (EventType, const std::string& name, const std::string& description),
                (override));
    MOCK_METHOD(void,
                finalize_data_definition,
                (DataType, const std::string& name, const std::string& description),
                (override));
    MOCK_METHOD(void,
                finalize_data_definition,
                (DataType, const std::string& name, const std::string& description, EnumType enum_type),
                (override));
    MOCK_METHOD(void, finalize_enum_definition, (EnumType, const EnumMappingType&), (override));

    MOCK_METHOD(void, enable, (), (override));
    MOCK_METHOD(void, disable, (), (override));

    // allow the test framework to call an otherwise protected method
    void do_catch_up() { catch_up(); }
    void skip()
    {
        EventStreamSink sink;
        definition_index = Definitions::instance().catch_up(sink, definition_index);
        enumeration_index = Enumerations::instance().catch_up(sink, enumeration_index);
    }

    std::unique_ptr<Definitions> defs;
    std::unique_ptr<Enumerations> enums;
};

struct MockEventAnnouncer : public EventAnnouncer
{
    MOCK_METHOD(void, post_event, (const std::string& event_name, std::uint64_t time), (override));
    MOCK_METHOD(void,
                add_int_data,
                (const std::string& event_name,
                 int id,
                 const std::string& data_type_name,
                 std::int64_t value,
                 std::uint64_t time),
                (override));
    MOCK_METHOD(void,
                add_uint_data,
                (const std::string& event_name, int id, const std::string& data_type_name, std::uint64_t value),
                (override));
    MOCK_METHOD(void,
                add_string_data,
                (const std::string& event_name, int id, const std::string& data_type_name, const std::string& value),
                (override));
};

} // namespace ampere::event_stream

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
