//
// Copyright(c) 2022 Ampere Computing. All Rights Reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include <fcntl.h>
#include <fmt/ostream.h>
#include <unistd.h>
#include <cstdint>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>

#include "ampere/event_stream/event_stream.pb.h"
#include "ampere/event_stream/event_stream_proto.h"
#include "ampere/protobuf_utils/protobuf_stream.h"

#include "ampere/evp/plugins/capture.h"
#include "ampere/evp/plugin.h"

namespace ampere::evp {

CaptureBase::CaptureBase(ProcessorIfc& proc_ifc, Args& args) : Plugin(proc_ifc)
{
    bool force = false;
    std::string ofile;

    args.pop(ofile);

    while (args) {
        if (args.pop("-f|--force")) {
            force = true;
        } else {
            break;
        }
    }

    std::optional<int> ofd;
    try {
        size_t pos{0};
        auto fd_candidate = std::stoi(ofile, &pos);
        if (pos == ofile.size())
            ofd = fd_candidate;
    } catch (const std::invalid_argument&) {
        if (ofile == "-") {
            ofd = STDOUT_FILENO;
            logging = false;
        }
    }

    if (ofd) {
        output_stream = std::make_unique<ProtobufStreamWriter>(
            *ofd, event_stream::protobuf_magic, event_stream::protobuf_es_version);
    } else {
        output_stream = std::make_unique<ProtobufStreamWriter>(
            ofile, event_stream::protobuf_magic, event_stream::protobuf_es_version, force);
    }

    if (!output_stream)
        throw std::runtime_error{"capture requires a filename or fd"};
}

void CaptureBase::define_event(const Definition& event_def)
{
    Record record;
    record.set_allocated_definition(const_cast<Definition*>(&event_def));
    output_stream->write(record);
    static_cast<void>(record.release_definition());
    ++num_event_defs;
}

void CaptureBase::define_value(const Definition& value_def)
{
    Record record;
    record.set_allocated_definition(const_cast<Definition*>(&value_def));
    output_stream->write(record);
    static_cast<void>(record.release_definition());
    ++num_value_defs;
}

void CaptureBase::define_enumeration(const Enumeration& enum_def)
{
    Record record;
    record.set_allocated_enumeration(const_cast<Enumeration*>(&enum_def));
    output_stream->write(record);
    static_cast<void>(record.release_enumeration());
    ++num_enum_defs;
}

void CaptureBase::report_parameter(const Parameter& parameter)
{
    Record record;
    record.set_allocated_parameter(const_cast<Parameter*>(&parameter));
    output_stream->write(record);
    static_cast<void>(record.release_parameter());
    ++num_parameters;
}

void CaptureBase::start_simulation()
{
    event_stream_proto::Control control;
    control.set_type(event_stream_proto::START_SIMULATION);
    Record record;
    record.set_allocated_control(&control);
    output_stream->write(record);
    static_cast<void>(record.release_control());
}

void CaptureBase::process_event(const Event& event)
{
    Record record;
    record.set_allocated_event(const_cast<Event*>(&event));
    output_stream->write(record);
    static_cast<void>(record.release_event());
    ++num_events;
}

void CaptureBase::end_simulation()
{
    output_stream->flush();
}

void CaptureBase::report(MetricTableTimeSeries& ts)
{
    if (logging)
        fmt::print(std::cerr,
                   "Captured {} event definitions, {} value definitions, {} enum definitions, {} parameter values, and "
                   "{} events.\n",
                   num_event_defs,
                   num_value_defs,
                   num_enum_defs,
                   num_parameters,
                   num_events);
}

AMPERE_EVP_PLUGIN_FROM(Capture, "capture", "Capture subset of the event stream", CaptureBase)
{
public:
    Capture(ProcessorIfc & proc_ifc, Args & args);

    static void help(int argc, const char** argv);
    void define_value(const Definition& value_def) override;
    void process_event(const Event& event) override;

private:
    std::map<std::string, std::variant<int64_t, uint64_t>> filter_data_by_name;
    std::unordered_map<int, std::variant<int64_t, uint64_t>> filter_data;

    bool filtering{false};

    void add_filter(const std::string& filter_spec);
    bool filter_match(const Event& event);
};

Capture::Capture(ProcessorIfc& proc_ifc, Args& args) : CaptureBase(proc_ifc, args)
{
    while (args) {
        std::string arg;

        if (args.pop("--filter", arg)) {
            add_filter(arg);
            filtering = true;
        } else {
            break;
        }
    }

    args.done();
}

void Capture::help(int argc, const char** argv)
{
    print_help(argv[0], "capture", "<output filename> [-f|--force] [--filter <filter spec>]", R"(Arguments:

    --force, -f                Overwrite output file if it exists
    --filter                   Only include events/transactions matching some data value (i.e. <data_name>=<data_value>)
    --help, -h                 This help message.
)");
}

void Capture::define_value(const Definition& value_def)
{
    CaptureBase::define_value(value_def);

    if (auto it = filter_data_by_name.find(value_def.name()); it != filter_data_by_name.end())
        filter_data[value_def.id()] = it->second;
}

void Capture::process_event(const Event& event)
{
    if (!filtering || filter_match(event))
        CaptureBase::process_event(event);
}

void Capture::add_filter(const std::string& filter_spec)
{
    auto eq = filter_spec.find('=');

    if (eq == std::string::npos)
        throw std::runtime_error{
            fmt::format("invalid filter spec (\"{}\") should be <data name>=<value>", filter_spec)};

    std::string data_name = filter_spec.substr(0, eq);
    std::string value_str = filter_spec.substr(eq + 1);

    if (value_str.size() > 2 && value_str[0] == '0' && value_str[1] == 'x')
        filter_data_by_name[data_name] = static_cast<uint64_t>(std::stoull(value_str, nullptr, 0));
    else
        filter_data_by_name[data_name] = static_cast<int64_t>(std::stoll(value_str, nullptr, 0));
}

bool Capture::filter_match(const Event& event)
{
    for (int i = 0; i < event.values_size(); i++) {
        const event_stream_proto::Value& value = event.values(i);
        auto it = filter_data.find(value.definition_id());

        if (it != filter_data.end()) {
            if (value.values_case() == event_stream_proto::Value::kIntValue) {
                if (std::holds_alternative<int64_t>(it->second))
                    return value.int_value() == std::get<int64_t>(it->second);
                else if (std::holds_alternative<uint64_t>(it->second))
                    return value.int_value() == static_cast<int64_t>(std::get<uint64_t>(it->second));
            } else if (value.values_case() == event_stream_proto::Value::kUintValue) {
                if (std::holds_alternative<int64_t>(it->second))
                    return value.uint_value() == static_cast<uint64_t>(std::get<int64_t>(it->second));
                else if (std::holds_alternative<uint64_t>(it->second))
                    return value.uint_value() == std::get<uint64_t>(it->second);
            }

            return false;
        }
    }

    return false;
}

} // namespace ampere::evp
