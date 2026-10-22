//
// Copyright (c) 2018-2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <iostream>
#include <unordered_map>
#include <boost/program_options.hpp>
namespace po = boost::program_options;

#include "ampere/event_stream/event_stream_reader.h"
#include "ampere/protobuf_utils/protobuf_utils.h"

#include <google/protobuf/io/zero_copy_stream_impl.h>

int main(int argc, char** argv)
{
    bool opt_hex{false};
    bool opt_show_enums{false};

    po::options_description desc("Allowed options");
    desc.add_options()("help", "produce help message")("hex,h", "print all event values in hexadecimal")(
        "show-enums", "include enum ids/values");

    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);

    opt_hex = vm.count("hex");
    opt_show_enums = vm.count("show-enums");

    if (1 == argc || vm.count("help")) {
        std::cerr << desc << "\n";
        std::cerr << "Usage: " << std::endl;
        std::cerr << "  " << argv[0] << " [opts] [path to stream file]" << std::endl;
        return 1;
    }

    ampere::event_stream::EventStreamReader es_reader(argv[argc - 1]);
    ampere::event_stream_proto::Record record;
    std::unordered_map<int, ampere::event_stream_proto::Definition> definitions;
    std::unordered_map<int, ampere::event_stream_proto::Enumeration> enumerations;

    std::cout << "version=" << es_reader.version() << std::endl;

    while (true) {
        // start to read through the file
        // if we reached the end, break out
        if (!es_reader.read(record))
            break;

        if (record.has_definition()) {
            const auto& definition = record.definition();
            definitions[record.definition().id()] = definition;
            std::cout << "definition"
                      << " name=" << definition.name() << " description=" << definition.description()
                      << " id=" << definition.id();

            if (opt_show_enums && definition.has_enumeration_id())
                std::cout << " enumeration=" << definition.enumeration_id();

            std::cout << std::endl;
        } else if (record.has_enumeration()) {
            const auto& enumeration = record.enumeration();
            enumerations[enumeration.id()] = enumeration;
            if (opt_show_enums) {
                std::cout << "enumeration"
                          << " id=" << enumeration.id();

                for (const auto& [id, name] : enumeration.values())
                    std::cout << " " << name << "=" << id;
                std::cout << std::endl;
            }
        } else if (record.has_parameter()) {
            const auto& parameter = record.parameter();
            std::cout << "parameter"
                      << " name=" << parameter.name() << " description=" << parameter.description() << " value=";
            auto which_value = parameter.value_case();
            switch (which_value) {
            case ampere::event_stream_proto::Parameter::kBoolValue:
                std::cout << (parameter.bool_value() ? "true" : "false");
                break;
            case ampere::event_stream_proto::Parameter::kIntValue:
                if (opt_hex)
                    std::cout << "0x" << std::hex;
                std::cout << parameter.int_value();
                break;
            case ampere::event_stream_proto::Parameter::kUintValue:
                if (opt_hex)
                    std::cout << "0x" << std::hex;
                std::cout << parameter.uint_value();
                break;
            case ampere::event_stream_proto::Parameter::kDoubleValue:
                std::cout << parameter.double_value();
                break;
            case ampere::event_stream_proto::Parameter::kStringValue:
                std::cout << parameter.string_value();
                break;
            case ampere::event_stream_proto::Parameter::kJsonValue:
                std::cout << parameter.json_value();
                break;
            case ampere::event_stream_proto::Parameter::VALUE_NOT_SET:
                std::cout << "<VALUE_NOT_SET>";
                break;
            }
            std::cout << std::endl;
        } else if (record.has_event()) {
            const auto& event = record.event();
            std::cout << "event" << std::dec << " time=" << event.time()
                      << " name=" << definitions[event.definition_id()].name();

            for (int i = 0; i < event.values_size(); i++) {
                const auto& value = event.values(i);
                auto definition = definitions[value.definition_id()];
                auto which_value = value.values_case();
                switch (which_value) {
                case ampere::event_stream_proto::Value::kIntValue:
                    std::cout << " " << definition.name() << "=";
                    if (!definition.has_enumeration_id() || !enumerations.contains(definition.enumeration_id())) {
                        if (opt_hex)
                            std::cout << "0x" << std::hex;
                        std::cout << value.int_value();
                    } else {
                        std::cout << enumerations.at(definition.enumeration_id()).values().at(value.int_value());
                    }
                    break;
                case ampere::event_stream_proto::Value::kUintValue:
                    std::cout << " " << definition.name() << "=";
                    if (!definition.has_enumeration_id() || !enumerations.contains(definition.enumeration_id())) {
                        if (opt_hex)
                            std::cout << "0x" << std::hex;
                        std::cout << value.uint_value();
                    } else {
                        std::cout << enumerations.at(definition.enumeration_id()).values().at(value.uint_value());
                    }
                    break;
                case ampere::event_stream_proto::Value::kStringValue:
                    std::cout << " " << definition.name() << "=" << value.string_value();
                    break;
                case ampere::event_stream_proto::Value::VALUES_NOT_SET:
                    break;
                }
            }
            std::cout << std::endl;
        } else if (record.has_control()) {
            std::cout << "control type=" << CtrlType_Name(record.control().type()) << std::endl;
        }
        record.Clear();
    }
    return 0;
}

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
