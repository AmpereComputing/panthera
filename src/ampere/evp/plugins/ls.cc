//
// Copyright(c) 2022 Ampere Computing. All Rights Reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include <fmt/ostream.h>
#include <algorithm>
#include <cmath>
#include <ranges>
#include <tuple>
#include <utility>
#include <cstdlib>

#include "ampere/evp/plugin.h"

namespace ampere::evp {

AMPERE_EVP_PLUGIN(List, "ls", "List event and/or data value definitions")
{
public:
    List(ProcessorIfc & proc_ifc, Args & args);

    static void help(int argc, const char** argv);
    static bool singular()
    {
        return true;
    }

    void define_event(const Definition& event_def) override;
    void define_value(const Definition& value_def) override;
    void define_enumeration(const Enumeration& enum_def) override;
    void start_simulation() override;

private:
    std::map<std::string, std::string, NatComp> events;
    std::map<std::string, std::string, NatComp> values;
    std::map<int, std::map<int64_t, std::string>> enum_values;

    bool list_events{false};
    bool list_values{false};
    std::unordered_map<std::string, int> list_enum;

    static void print_enum_values(const std::map<int64_t, std::string>& values);
};

List::List(ProcessorIfc& proc_ifc, Args& args) : Plugin{proc_ifc}
{
    if (!args)
        list_events = true;

    std::string arg_value;
    while (args) {
        if (args.pop("--events"))
            list_events = true;
        else if (args.pop("--values"))
            list_values = true;
        else if (args.pop("--enum", arg_value))
            list_enum.emplace(arg_value, -1);
        else
            break;
    }

    args.done();
}

void List::help(int argc, const char** argv)
{
    print_help(argv[0], "ls", "[--events] [--values]", R"(Arguments:

    --events                   List event definitions
    --values                   List data-value definitions
    --enum <value name>        List enumeration values for value definition
    --help, -h                 This help message.
)");
}

void List::define_event(const Definition& event_def)
{
    if (list_events)
        events[event_def.name()] = event_def.description();
}

void List::define_value(const Definition& value_def)
{
    if (list_values)
        values[value_def.name()] = value_def.description();

    if (!list_enum.empty() && value_def.has_enumeration_id()) {
        if (auto it = list_enum.find(value_def.name()); it != list_enum.end())
            it->second = value_def.enumeration_id();
    }
}

void List::define_enumeration(const Enumeration& enum_def)
{
    auto values = std::views::values(list_enum);
    if (std::ranges::find(values, enum_def.id()) != values.end())
        enum_values.emplace(std::piecewise_construct,
                            std::forward_as_tuple(enum_def.id()),
                            std::forward_as_tuple(enum_def.values().begin(), enum_def.values().end()));
}

void List::print_enum_values(const std::map<int64_t, std::string>& values)
{
    auto longest = std::log10(std::ranges::max(std::views::keys(values)));
    for (const auto& [key, value] : values)
        fmt::print("enum  value[{:>{}}] {}\n", key, static_cast<unsigned>(std::ceil(longest)), value);
}

void List::start_simulation()
{
    size_t longest_name{0};

    for (const auto& ev : events)
        longest_name = std::max(longest_name, ev.first.length());

    for (const auto& ev : values)
        longest_name = std::max(longest_name, ev.first.length());

    for (const auto& [name, description] : events)
        fmt::print("event {:<{}} {}\n", name, longest_name, description);

    for (const auto& [name, description] : values) {
        fmt::print("value {:<{}} {}\n", name, longest_name, description);
        if (auto it = list_enum.find(name); it != list_enum.end())
            print_enum_values(enum_values[it->second]);
    }

    if (!list_values && !list_enum.empty()) {
        for (const auto& [id, values] : enum_values) {
            const auto& name =
                std::find_if(list_enum.begin(), list_enum.end(), [&](const auto& e) { return e.second == id; })->first;

            fmt::print("value {}\n", name);
            print_enum_values(values);
        }
    }

    exit(0);
}

} // namespace ampere::evp
