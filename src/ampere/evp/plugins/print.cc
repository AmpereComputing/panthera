//
// Copyright(c) 2022 Ampere Computing. All Rights Reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include <fmt/ostream.h>

#include "ampere/evp/plugin.h"

namespace ampere::evp {

AMPERE_EVP_PLUGIN(Print, "print", "Print events")
{
public:
    Print(ProcessorIfc & proc_ifc, Args & args);

    static bool singular()
    {
        return true;
    }
    static void help(int argc, const char** argv);
    void process_event(const Event& event) override;

    template<typename T>
    void print_expanded_value(const event_stream_proto::Definition* definition, T value)
    {
        if (definition && expand_enumerations) {
            if (const auto* name = enumeration_value_for_definition(*definition, value); name) {
                fmt::print("{}", name);
                return;
            }
        }

        fmt::print("{}", value);
    }

private:
    bool expand_enumerations = true;
};

Print::Print(ProcessorIfc& proc_ifc, Args& args) : Plugin{proc_ifc}
{
    while (args) {
        if (args.pop("--no-enum"))
            expand_enumerations = false;
        else
            break;
    }

    args.done();
}

void Print::help(int argc, const char** argv)
{
    print_help(argv[0], "print", "[--enum]", R"(Arguments:

    --enum                     Expand enumerations into string values
)");
}

void Print::process_event(const Event& event)
{
    fmt::print("{} {}", event.time(), get_definition(event.definition_id()).name());

    for (const auto& value : event.values()) {
        event_stream_proto::Definition const* definition = nullptr;
        try {
            definition = &get_definition(value.definition_id());
            fmt::print(" {}=", definition->name());
        } catch (std::out_of_range& exc) {
            fmt::print(std::cerr, "bad definition (id {})\n", value.definition_id());
            continue;
        }

        switch (value.values_case()) {
        case event_stream_proto::Value::kIntValue:
            print_expanded_value(definition, value.int_value());
            break;
        case event_stream_proto::Value::kUintValue:
            print_expanded_value(definition, value.uint_value());
            break;
        case event_stream_proto::Value::kStringValue:
            fmt::print("{}", value.string_value());
            break;
        default:
            throw std::runtime_error{
                fmt::format("unknown value type {} in event", static_cast<unsigned>(value.values_case()))};
        }
    }

    fmt::print("\n");
}

} // namespace ampere::evp
