//
// Copyright(c) 2022 Ampere Computing. All Rights Reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include <fcntl.h>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <cstring>
#include <utility>

#include <fmt/format.h>
#include <fmt/ostream.h>
#include <fmt/ranges.h>

#include "ampere/util/exception_formatting.h"

#include "args.h"
#include "processor.h"
#include "utils.h"

template<>
struct fmt::formatter<std::filesystem::path> : fmt::ostream_formatter
{};

namespace ampere::evp {

void expand_file_args(std::list<std::string>& args, const std::list<fs::path>& search_paths, const fs::path& from);

/** Read a file and turn the space-separated words into a list of
 *  arguments.
 *
 *  This uses the io manipulator "quote" as an attempt to treat quoted
 *  strings as single arguments. It's useful, but not nearly as potent
 *  as a shell-quote, so this might need improvement.
 */
std::pair<bool, std::list<std::string>> try_rendering_file_as_args(const fs::path& filename,
                                                                   const std::list<fs::path>& search_paths)
{
    if (!fs::exists(filename))
        return {false, {}};

    std::list<std::string> args;
    std::ifstream in{filename.native()};
    std::string line;

    while (std::getline(in, line)) {
        std::istringstream ss(line);

        while (ss) {
            std::string arg;

            ss >> std::quoted(arg);
            if (!arg.empty()) {
                // Any token that begins with char '#' is a comment until the end of the line
                if (arg[0] == '#') {
                    break;
                }

                args.push_back(arg);
            }
        }
    }

    expand_file_args(args, search_paths, filename.parent_path());

    return {true, args};
}

/** Interpolate file contents into command line arguments.
 *
 *  Every command line argument of the form "@name" is potentially a
 *  reference to a file "name" ("name" can be a path). If we can find
 *  that file, either exactly as written or in the config_path, then
 *  we'll read its contents in and insert those arguments into the
 *  given list "args".
 */
void expand_file_args(std::list<std::string>& args, const std::list<fs::path>& search_paths, const fs::path& from)
{
    for (auto it = args.begin(); it != args.end();) {
        if ((*it)[0] != '@') {
            ++it;
            continue;
        }

        bool expanded = false;
        fs::path argfile = it->substr(1);
        if (auto [found, config_args] = try_rendering_file_as_args(from / argfile, search_paths); found) {
            it = args.erase(it);
            args.splice(it, config_args);
            continue;
        } else if (it->substr(1, 2) != "./" && it->substr(1, 3) != "../") {
            // Only check search paths if non explicitly relative (i.e. does not start with "./")
            for (const auto& config_path : search_paths) {
                auto [found, config_args] = try_rendering_file_as_args(config_path / argfile, search_paths);
                expanded |= found;

                if (found) {
                    it = args.erase(it);
                    args.splice(it, config_args);
                    break;
                }
            }
        }

        if (!expanded)
            throw std::runtime_error{fmt::format("could not find config file {}", argfile)};
    }
}

/** Simple container for a plugin name and argument list.
 */
struct PluginWithArgs
{
    std::string name;
    Args args;

    PluginWithArgs(const std::string& name) : name{name} {}
};

using PluginsWithArgs = std::list<PluginWithArgs>;

/** Partition command line arguments into program arguments and plugin
 *  arguments.
 *
 *  We start by expanding file references into lists of arguments.
 *
 *  Arguments are assumed to be targeted at the main program until we
 *  reach the first plugin name, which will be prefixed with "+". Then
 *  we start parsing arguments as plugin arguments.
 *
 *  So the command line is structured like this, for example:
 *
 *    evp --prog_arg1 prog_arg2                         \
 *        +plugin1 plugin1_arg1 --plugin1_arg2          \
 *        +plugin2                                      \
 *        +plugin3 -plugin3_arg1
 */
std::tuple<Args, PluginsWithArgs> partition_args(int argc, const char** argv)
{
    std::list<std::string> args(&argv[1], &argv[argc]);
    std::list<fs::path> config_search_paths = {get_config_path()};
    expand_file_args(args, config_search_paths, ".");

    Args prog_args;
    PluginsWithArgs plugins;
    PluginWithArgs* in_plugin = nullptr;

    while (!args.empty()) {
        auto arg = args.front();

        if (arg[0] == '+' || arg == "-P" || arg == "--plugin") {
            std::string name;
            if (arg[0] == '+') {
                name = arg.substr(1);
            } else {
                args.pop_front();
                if (args.empty())
                    throw std::runtime_error{fmt::format("missing plugin name after \"{}\"", arg)};
                name = args.front();
            }

            in_plugin = nullptr;
            if (auto p = registered_plugins().find(name); p != registered_plugins().end() && p->second.signular()) {
                auto existing =
                    std::find_if(plugins.begin(), plugins.end(), [&](const auto& p) { return p.name == name; });

                if (existing != plugins.end())
                    in_plugin = &*existing;
            }

            if (!in_plugin)
                in_plugin = &plugins.emplace_back(name);
        } else if (in_plugin) {
            if (Plugin::is_help(arg)) {
                if (auto p = registered_plugins().find(in_plugin->name); p != registered_plugins().end())
                    p->second.help(argc, argv);
            }

            in_plugin->args.push(arg);
        } else {
            prog_args.push(arg);
        }

        args.pop_front();
    }

    return {prog_args, plugins};
}

/** Create a new plugin with associated arguments.
 *
 *  The plugin must have previously registered itself in the global
 *  registry.
 */
void build_plugin(Processor& processor, PluginWithArgs& plugin)
{
    if (auto p = registered_plugins().find(plugin.name); p != registered_plugins().end()) {
        try {
            processor.add_plugin(p->second.factory(processor, plugin.args));
        } catch (...) {
            std::throw_with_nested(std::runtime_error{fmt::format("issue constructing plugin \"{}\"", plugin.name)});
        }
    } else
        throw std::runtime_error{fmt::format("could not find plugin \"{}\"", plugin.name)};
}

/** Build all plugins.
 */
void build_plugins(Processor& processor, PluginsWithArgs& plugins)
{
    for (auto& plugin : plugins)
        build_plugin(processor, plugin);
}

/** This is a silly wrapper around a C file descriptor so that we can
 *  ensure it gets closed when the object is destroyed.
 */
struct CFile
{
    int fd{0};

    ~CFile()
    {
        if (fd > 0)
            close(fd);
    }

    void open_file(const std::string& fname)
    {
        fd = open(fname.c_str(), O_RDONLY);

        if (fd < 0)
            throw std::runtime_error{fmt::format("{}: {}", fname, strerror(fd))};
    }
};

void dump(const Args& args, const PluginsWithArgs& plugins)
{
    if (args.begin() != args.end())
        fmt::print("global options:\n  {}\n\n", fmt::join(args.begin(), args.end(), " "));

    std::map<std::string, unsigned> instances;
    for (const auto& plugin : plugins) {
        fmt::print("{}[{}] plugin options:\n  ", plugin.name, instances[plugin.name]++);
        bool as_value = false;
        size_t i = 0;
        for (const auto& arg : plugin.args) {
            auto is_arg = arg[0] == '-';
            std::cout << (!is_arg && as_value ? " " : ((i++) ? "\n  " : "")) << arg;
            as_value = is_arg;
        }
        std::cout << "\n\n";
    }

    std::cout << std::endl;

    exit(EXIT_FAILURE);
}

void help(const char* prog)
{
    constexpr const char* usage = R"(Usage:

    {} [global args...] [plugin args...] [plugin args...]...

Where global args are:

    --es FILENAME.es     Input event stream file. Default stdin
    --in_fd NUM          Input file descriptor.
    --out_fd NUM         Output file descriptor.
    -i INTERVAL          Collect metrics periodically on a time (10us) or metric
                         count interval (see docs for details).
    --start <trigger>    Time or data_value of when to start processing.
    --stop <trigger>     Time or data_value of when to stop processing.
    --exit               Exit evp after stop is reached
    -s <name>=<value>    Set a variable to a value (variables are accessible to
                         all plugins).
    --dump               Dump plugins and their arguments
    --help, -h           This help message.

Plugin arguments are introduced with a plugin name followed by arguments
specific to that plugin. E.g.:

    -P|--plugin <plugin name> [<plugin arg1> <plugin arg2>...]

or

    +<plugin name> [<plugin arg1> <plugin arg2>...]

Groups of arguments may also be supplied by proving a file @knobs.cfg;
See the documentation for details of how that file is located.

Available Plugins:

)";

    fmt::print(usage, prog);
    for (const auto& [name, plugin_def] : registered_plugins())
        fmt::print("    {:<12} {:<12} {}\n", name, plugin_def.category(), plugin_def.description);
    std::cout << std::endl;

    exit(EXIT_FAILURE);
}

} // namespace ampere::evp

using namespace ampere::evp;

extern "C" int main(int argc, const char** argv)
{
    try {
        set_config_path();
        auto [args, plugins] = partition_args(argc, argv);

        CFile input;
        int in_fd{0}, out_fd{0};
        std::string input_es;
        std::string interval;
        std::map<std::string, std::string> variables;
        std::string start;
        std::string stop;
        bool exit_after_stop = false;

        while (args) {
            std::string var_setting;

            if (args.pop("--es", input_es)) {
                ;
            } else if (args.pop("--in_fd", in_fd)) {
                ;
            } else if (args.pop("--out_fd", out_fd)) {
                ;
            } else if (args.pop("-i", interval)) {
                ;
            } else if (args.pop("--start", start)) {
                ;
            } else if (args.pop("--stop", stop)) {
                ;
            } else if (args.pop("--exit")) {
                exit_after_stop = true;
            } else if (args.pop("--dump")) {
                dump(args, plugins);
            } else if (args.pop("--help") || args.pop("-h")) {
                help(std::filesystem::path(argv[0]).filename().c_str());
            } else if (args.pop("-s", var_setting)) {
                size_t pos = var_setting.find('=');
                if (pos == std::string::npos)
                    throw std::runtime_error(
                        fmt::format("variable setting should be <name>=<value> (got \"{}\")", var_setting));
                auto var = var_setting.substr(0, pos);
                auto value = var_setting.substr(pos + 1);
                variables[var] = value;
            } else {
                break;
            }
        }

        args.done();

        std::unique_ptr<Processor> processor;

        if (!input_es.empty())
            processor = std::make_unique<Processor>(input_es, variables);
        else
            processor = std::make_unique<Processor>(in_fd, out_fd, variables);

        processor->initialize();

        CounterSet skip_counters;
        uint64_t skip_time = 0;
        if (!start.empty()) {
            if (is_time_spec(start)) {
                skip_time = parse_time_spec(start);
                if (skip_time == 0)
                    start.clear();
            } else {
                Action stop_skip = [&](Counter* counter, const Event& event) {
                    processor->stop_skipping();
                };
                processor->on(nullptr, start, stop_skip, &skip_counters);
                if (auto counter = skip_counters.begin();
                    counter != skip_counters.end() && processor->get_counter(*counter).get_trip() == 0)
                    start.clear();
            }
        }
        if (!stop.empty()) {
            processor->at_or_on(
                nullptr,
                stop,
                [&, exit_after_stop] { processor->stop_at_previous_time(exit_after_stop); },
                &skip_counters);
        }

        build_plugins(*processor, plugins);

        if (!interval.empty()) {
            if (is_time_spec(interval)) {
                TimeAction collect = [&](uint64_t current_time, uint64_t expiry) {
                    processor->collect(expiry);
                };
                processor->at_every(nullptr, interval, collect);
            } else {
                Action collect = [&](Counter* counter, const Event& event) {
                    processor->collect();
                };
                processor->on_every(nullptr, interval, collect);
            }
        }

        processor->start();

        if (!start.empty())
            processor->skip(skip_time, &skip_counters);

        processor->process();
        processor->report();

        if (!exit_after_stop)
            processor->skip_remaining();
    } catch (const std::runtime_error& err) {
        ampere::util::print_exception(std::cerr, err);
        exit(EXIT_FAILURE);
    }

    return 0;
}
