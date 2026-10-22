//
// Copyright(c) 2022 Ampere Computing. All Rights Reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include <regex>
#include <string>
#include <vector>
#include <algorithm>

#include "ampere/evp/plugin.h"
#include "ampere/evp/utils.h"

namespace ampere::evp {

AMPERE_EVP_PLUGIN(Rename, "rename", "Rename or copy metrics")
{
public:
    Rename(ProcessorIfc & proc_ifc, Args & args);

    static void help(int argc, const char** argv);

    void report(MetricTableTimeSeries & ts) override;

private:
    struct Renamer
    {
        enum Type : unsigned {
            PLAIN = 0,
            COPY = 1 << 0,
            LOWERCASE = 1 << 1,
        };

        std::regex from;
        std::string to;
        unsigned type{PLAIN};
        Renamer(std::regex from, std::string to, unsigned type = PLAIN) : from(from), to(to), type(type) {}

        bool copies() const { return type & COPY; }
        bool matches(const std::string& name) const { return std::regex_match(name, from); }
        std::string rename(const std::string& name) const
        {
            auto replaced = std::regex_replace(name, from, to);
            if (type & LOWERCASE)
                std::transform(
                    replaced.begin(), replaced.end(), replaced.begin(), [](auto c) { return std::tolower(c); });
            return replaced;
        }
    };

    std::vector<Renamer> renames;
    using renamer_iterator = decltype(renames.begin());

    void add_rename(const std::string& arg, unsigned type = Renamer::PLAIN);
    void rename_column(std::string column, MetricTableTimeSeries & ts, renamer_iterator start) const;
};

Rename::Rename(ProcessorIfc& proc_ifc, Args& args) : Plugin{proc_ifc}
{
    while (args) {
        std::string arg;

        if (args.pop("-r", arg))
            add_rename(arg);
        else if (args.pop("-rl", arg))
            add_rename(arg, Renamer::LOWERCASE);
        else if (args.pop("-c", arg))
            add_rename(arg, Renamer::COPY);
        else if (args.pop("-cl", arg))
            add_rename(arg, Renamer::LOWERCASE | Renamer::COPY);
        else
            break;
    }

    args.done();
}

void Rename::help(int argc, const char** argv)
{
    print_help(argv[0], "rename", "[-r <old name>=<new name>] [-c <old name>=<new name>]", R"(Arguments:

    -r <old name>=<new name>    Rename an old metric name to a new one
    -rl <old name>=<new name>   Rename an old metric name to a new one, and convert to lowercase
    -c <old name>=<new name>    Copy an old metric name to a new one
    -cl <old name>=<new name>   Copy an old metric name to a new one, and convert to lowercase
    --help, -h                  This help message.
)");
}

void Rename::add_rename(const std::string& arg, unsigned type)
{
    size_t pos = arg.find('=');

    if (pos == std::string::npos)
        throw std::runtime_error{"argument to rename must be <pattern>=<replacement>"};

    auto regex_str = glob_to_pattern(arg.substr(0, pos));
    auto replacement = arg.substr(pos + 1);

    try {
        renames.emplace_back(regex(regex_str), replacement, type);
    } catch (std::regex_error& err) {
        throw std::runtime_error{fmt::format("busted regex: {}", regex_str)};
    }
}

void Rename::rename_column(std::string column, MetricTableTimeSeries& ts, renamer_iterator start) const
{
    for (auto renamer = start; renamer != renames.end(); ++renamer) {
        if (renamer->matches(column)) {
            auto new_name = renamer->rename(column);
            if (renamer->copies()) {
                ts.add_col({new_name, ts[column]});
                rename_column(new_name, ts, std::next(renamer));
            } else {
                ts.rename(column, new_name);
                column = new_name;
            }
        }
    }
}

void Rename::report(MetricTableTimeSeries& ts)
{
    for (const auto& column : ts.columns())
        rename_column(column, ts, renames.begin());
}

} // namespace ampere::evp
