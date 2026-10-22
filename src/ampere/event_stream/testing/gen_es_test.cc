//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include <fstream>
#include <iostream>
#include <list>
#include <regex>
#include <string>
#include <vector>

#include <fmt/format.h>
#include <fmt/ostream.h>

#include "ampere/event_stream/event_stream.h"
#include "ampere/event_stream/event_stream_proto.h"
#include "ampere/protobuf_utils/protobuf_stream.h"

using namespace ampere::event_stream;

std::list<std::string> file_to_list(std::istream& in)
{
    static std::regex ws{"^[ \t]*"};

    std::list<std::string> contents;
    std::string line;

    while (std::getline(in, line)) {
        line = std::regex_replace(line, ws, "");

        auto comment = line.find('#');

        if (comment != std::string::npos)
            line.erase(comment);

        if (!line.empty())
            contents.push_back(line);
    }

    return contents;
}

struct Event
{
    uint64_t time;
    std::string event_name;
    EventType event_type;

    struct Data
    {
        std::string data_name;
        std::string value;
        DataType data_type;
    };

    std::list<Data> data;
};

struct Param
{
    std::string param_name;
    std::string value;
};

using Enum = EnumMappingType;

std::list<Event> text_to_events(const std::list<std::string>& lines)
{
    static std::regex event{R"(^(\d+)\s+(\S+)((?:\s+(\S+)=(\S+))*)\s*$)"}; //, std::regex_constants::ECMAScript};
    static std::regex data{R"(\s*(\S+)=(\S+))"};

    std::list<Event> events;

    for (const auto& line : lines) {
        std::smatch m;

        if (std::regex_match(line, m, event)) {
            auto time = std::stoull(m[1]);
            const auto& event_name = m[2];

            events.push_back({time, event_name});

            if (m.size() > 3) {
                const auto& data_values = m[3].str();
                for (auto it = std::sregex_iterator(data_values.begin(), data_values.end(), data),
                          data_end = std::sregex_iterator();
                     it != data_end;
                     ++it)
                {
                    const std::smatch& data_match = *it;
                    events.back().data.push_back({data_match[1].str(), data_match[2].str()});
                }
            }
        }
    }

    return events;
}

std::list<Param> text_to_params(const std::list<std::string>& lines)
{
    static std::regex param{R"(^param\s+(\S+)=(\S+)\s*$)"};

    std::list<Param> params;

    for (const auto& line : lines) {
        std::smatch m;

        if (std::regex_match(line, m, param)) {
            params.push_back({m[1], m[2]});
        }
    }

    return params;
}

std::map<std::string, std::pair<unsigned, Enum>> text_to_enums(const std::list<std::string>& lines)
{
    static std::regex enum_def{R"(^enum\s+(\w+)\s+(.*)$)"};
    static std::regex enum_value{R"(\s*(\w+)=(\d+))"};

    std::map<std::string, std::pair<unsigned, Enum>> enums;

    for (const auto& line : lines) {
        std::smatch m;

        if (std::regex_match(line, m, enum_def)) {
            auto e = enums.emplace(m[1].str(), std::make_pair(enums.size(), Enum{})).first;
            const auto& values = m[2].str();
            for (auto it = std::sregex_iterator(values.begin(), values.end(), enum_value),
                      data_end = std::sregex_iterator();
                 it != data_end;
                 ++it)
            {
                const std::smatch& sm = *it;
                e->second.second.emplace(std::stoi(sm[2]), sm[1]);
            }
        }
    }

    return enums;
}

void generate_es(const std::list<Param>& params,
                 const std::map<std::string, std::pair<unsigned, Enum>> enums,
                 std::list<Event> events,
                 EventStream& es)
{
    static std::regex digits{"^\\d+$"};
    static std::regex udigits{"^\\d+u$"};
    static std::regex jsonlike{"^[[{(].*[\\]})]$"};

    std::map<std::string, EventType> all_events;
    std::map<std::string, DataType> all_data;

    // First pass: define all events and data, send parameters
    for (auto& event : events) {
        if (!all_events.contains(event.event_name))
            all_events[event.event_name] = es.define_event(event.event_name, "event");

        event.event_type = all_events[event.event_name];

        for (auto& data : event.data) {
            if (!all_data.contains(data.data_name)) {
                if (auto e = enums.find(data.data_name); e != enums.end()) {
                    auto enum_type = es.define_enum(e->second.first, e->second.second);
                    all_data[data.data_name] = es.define_data(data.data_name, "data", enum_type);
                } else {
                    all_data[data.data_name] = es.define_data(data.data_name, "data");
                }
            }

            data.data_type = all_data[data.data_name];
        }
    }

    for (const auto& param : params) {
        if (std::regex_match(param.value, digits))
            es.set_int_parameter(param.param_name, param.param_name, std::stoi(param.value));
        else if (std::regex_match(param.value, udigits))
            es.set_uint_parameter(param.param_name, param.param_name, std::stoull(param.value));
        else if (std::regex_match(param.value, jsonlike))
            es.set_json_parameter(param.param_name, param.param_name, param.value);
        else
            es.set_string_parameter(param.param_name, param.param_name, param.value);
    }

    es.start_simulation();

    // Second pass: emit events with data
    for (const auto& event : events) {
        if (event.data.empty()) {
            es.post_event(event.event_type, event.time);
        } else {
            auto* eh = es.open_event(event.event_type, event.time);
            for (const auto& data : event.data) {
                if (std::regex_match(data.value, digits))
                    es.add_int_data(eh, data.data_type, std::stoi(data.value));
                else if (std::regex_match(data.value, udigits))
                    es.add_uint_data(eh, data.data_type, std::stoull(data.value));
                else
                    es.add_string_data(eh, data.data_type, data.value);
            }
            es.close_event(eh);
        }
    }
}

int main(int argc, const char** argv)
{
    std::vector<std::string> args(&argv[1], &argv[argc]);

    if (args.size() < 2) {
        fmt::print(std::cerr, "usage: {} <input file> <output file>\n", argv[0]);
        exit(1);
    }

    std::ifstream in{args[0]};
    EventStreamProto es{args[1]};
    auto lines = file_to_list(in);
    auto params = text_to_params(lines);
    auto enums = text_to_enums(lines);
    auto events = text_to_events(lines);
    generate_es(params, enums, events, es);
    return 0;
}
