//
// Copyright (c) 2018-2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>

#include <fmt/base.h>
#include <fmt/ostream.h>

#include <boost/pool/object_pool.hpp>

#include "ampere/event_stream/event_stream.h"
#include "ampere/event_stream/event_stream.pb.h"
#include "ampere/protobuf_utils/protobuf_stream.h"

namespace ampere::event_stream {

const uint32_t protobuf_magic = 0x53454250; // 0x50(P) 0x42(B) 0x45(E) 0x53(S)
const uint32_t protobuf_es_version = 4;     // synchronize with event_stream.py

class EventStreamProto;

class EventHandleProto : public EventHandleBase
{
    event_stream_proto::Event event_proto;
    friend class EventStreamProto;
};

class EventStreamProto : public EventStream
{
public:
    explicit EventStreamProto(int fd);
    explicit EventStreamProto(int fd_writer, int fd_reader);
    explicit EventStreamProto(const std::string& filename) : EventStreamProto(open_file(filename)) {}

    void post_event(EventType event_type, std::uint64_t time) override;
    EventHandle open_event(EventType event_type, std::uint64_t time) override;
    void close_event(EventHandle event) override;
    void add_int_data(EventHandle event, DataType event_data_type, std::int64_t value) override;
    void add_uint_data(EventHandle event, DataType event_data_type, std::uint64_t value) override;
    void add_string_data(EventHandle event, DataType event_data_type, const std::string& value) override;
    void set_bool_parameter(const std::string& name, const std::string& description, bool value) override;
    void set_int_parameter(const std::string& name, const std::string& description, std::int64_t value) override;
    void set_uint_parameter(const std::string& name, const std::string& description, std::uint64_t value) override;
    void set_double_parameter(const std::string& name, const std::string& description, double value) override;
    void set_string_parameter(const std::string& name,
                              const std::string& description,
                              const std::string& value) override;
    void set_json_parameter(const std::string& name, const std::string& description, const std::string& value) override;
    void start_simulation() override;
    void enable() override { enabled = true; }
    void disable() override { enabled = false; }
    bool is_enabled() override { return enabled; }

private:
    std::unique_ptr<ProtobufStreamWriter> ev_writer;
    std::unique_ptr<ProtobufStreamReader> ev_reader;
    boost::object_pool<EventHandleProto> event_handle_pool{32, 0};
    std::mutex output_mtx;

    int num_events{0};

    std::atomic<std::uint64_t> current_event_id;
    bool enabled{true};

    static int open_file(const std::string& filename);

    void finalize_event_definition(EventType event_type,
                                   const std::string& name,
                                   const std::string& description) override;
    void finalize_data_definition(DataType data_type, const std::string& name, const std::string& description) override;
    void finalize_data_definition(DataType data_type,
                                  const std::string& name,
                                  const std::string& description,
                                  EnumType enumeration) override;
    void finalize_enum_definition(EnumType enum_type, const EnumMappingType& enumerations) override;

    void write(event_stream_proto::Definition& definition);
    void write(event_stream_proto::Definition& definition, int id, bool& enabled);
    void write(event_stream_proto::Enumeration& enumeration);
    void write(event_stream_proto::Event& event);
    void write(event_stream_proto::Parameter& parameter);
    void write(event_stream_proto::Control& control);
    void write(event_stream_proto::Record& record);
    void write_read(event_stream_proto::Record& record, event_stream_proto::Response& response);
};

} // namespace ampere::event_stream

template<>
struct fmt::formatter<ampere::event_stream_proto::Kind> : fmt::ostream_formatter
{};
template<>
struct fmt::formatter<ampere::event_stream_proto::CtrlType> : fmt::ostream_formatter
{};
template<>
struct fmt::formatter<ampere::event_stream_proto::Record::RecordsCase> : fmt::ostream_formatter
{};

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
