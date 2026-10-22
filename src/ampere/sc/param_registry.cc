//
// Copyright (c) 2018-2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/sc/param.h"
#include "ampere/sc/param_registry.h"
#include "ampere/sc/event.h"
#include "ampere/sc/environment.h"
#include "ampere/sc/reader.h"
#include "ampere/sc/report.h"

#include <fmt/format.h>
#include <algorithm>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/path.hpp>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <ranges>
#include <regex>
#include <stdexcept>
#include <string>

#include <boost/iostreams/char_traits.hpp>
#include <boost/iostreams/concepts.hpp>
#include <boost/iostreams/device/file.hpp>
#include <boost/iostreams/filtering_stream.hpp>
#include <boost/iostreams/operations.hpp>
#include <boost/range/adaptor/reversed.hpp>
#include <optional>
#include <utility>

namespace ampere::sc {

using namespace std::literals::string_view_literals;

namespace parameter_specifications {
void value_to_event_stream_bool(EventStream& event_stream,
                                const std::string& name,
                                const std::string& description,
                                const json& value)
{
    bool bool_value = from_json_T<bool>(value);
    event_stream.set_bool_parameter(name, description, bool_value);
}
void value_to_event_stream_int(EventStream& event_stream,
                               const std::string& name,
                               const std::string& description,
                               const json& value)
{
    auto int_value = from_json_T<std::int64_t>(value);
    event_stream.set_int_parameter(name, description, int_value);
}
void value_to_event_stream_uint(EventStream& event_stream,
                                const std::string& name,
                                const std::string& description,
                                const json& value)
{
    auto uint_value = from_json_T<std::uint64_t>(value);
    event_stream.set_uint_parameter(name, description, uint_value);
}
void value_to_event_stream_double(EventStream& event_stream,
                                  const std::string& name,
                                  const std::string& description,
                                  const json& value)
{
    auto double_value = from_json_T<double>(value);
    event_stream.set_double_parameter(name, description, double_value);
}
void value_to_event_stream_string(EventStream& event_stream,
                                  const std::string& name,
                                  const std::string& description,
                                  const json& value)
{
    auto string_value = from_json_T<std::string>(value);
    event_stream.set_string_parameter(name, description, string_value);
}
void value_to_event_stream_json(EventStream& event_stream,
                                const std::string& name,
                                const std::string& description,
                                const json& value)
{
    event_stream.set_json_parameter(name, description, value.dump());
}
} /* namespace parameter_specifications */

ParameterRegistry::ParameterRegistry(std::initializer_list<std::pair<std::string, json>> settings)
{
    add_settings(settings, "constructor");
}

std::string escape_dots(const std::string& pattern)
{
    static const std::regex dot{"\\."};
    static const std::string escaped_dot{"\\."};
    return std::regex_replace(pattern, dot, escaped_dot);
}

std::string attach_prefix(const std::string& prefix, std::string name)
{
    static const std::string escaped_dot{"\\."};
    static const std::string tilde{"~"};

    if (prefix.empty())
        return name;
    if (name.empty())
        return prefix;

    bool is_addendum = false;

    if (!name.empty() && name[0] == '+') {
        is_addendum = true;
        name = name.substr(1);
    }

    bool prefix_is_regex = prefix[0] == '~';
    bool name_is_regex = !name.empty() && name[0] == '~';

    if (prefix_is_regex && name_is_regex)
        name = tilde + prefix.substr(1) + escaped_dot + name.substr(1);
    else if (prefix_is_regex)
        name = prefix + escaped_dot + escape_dots(name);
    else if (name_is_regex)
        name = tilde + escape_dots(prefix) + escaped_dot + name.substr(1);
    else if (name[0] == ':')
        name = prefix + name;
    else
        name = prefix + "." + name;

    if (is_addendum)
        name = fmt::format("+{}", name);

    return name;
}

std::pair<ParameterRegistry::ConfigScopeAction, std::string> extract_cfg_prefix(const std::string& pattern)
{
    static constexpr char scoped_cfg = ':';
    static constexpr std::string_view cfg = "cfg"sv;

    if (pattern.length() >= cfg.length()) {
        if (std::string_view last = {&pattern[pattern.length() - cfg.length()], cfg.length()}; last == cfg) {
            if (pattern.length() == cfg.length())
                return {ParameterRegistry::ConfigScopeAction::CREATE, ""};

            if (auto colon = pattern.rfind(scoped_cfg); colon != std::string::npos) {
                std::string_view type(&pattern[colon + 1], pattern.length() - colon - 1);
                auto action = ParameterRegistry::ConfigScopeAction::INVALID;
                if (type == cfg)
                    action = ParameterRegistry::ConfigScopeAction::CREATE;
                else if (type == "override-cfg")
                    action = ParameterRegistry::ConfigScopeAction::OVERRIDE;
                else if (type == "append-cfg")
                    action = ParameterRegistry::ConfigScopeAction::APPEND;
                else
                    throw std::runtime_error("Invalid config type");
                return {action, pattern.substr(0, colon)};
            }
        }
    }

    return {ParameterRegistry::ConfigScopeAction::INVALID, {}};
}

std::vector<std::tuple<bool, ParameterRegistry*, ParameterRegistry::Setting*>> ParameterRegistry::find_cfg_settings(
    const std::string& prefix)
{
    std::vector<std::tuple<bool, ParameterRegistry*, Setting*>> settings;
    for (const auto& [name, setting] : cfg_settings) {
        if (!setting->subregistry)
            continue;

        if (name == prefix || name.starts_with(prefix + ".")) {
            settings.emplace_back(name == prefix, this, setting);
        } else if (auto sub_settings = setting->subregistry->find_cfg_settings(prefix); !sub_settings.empty()) {
            settings.insert(settings.end(),
                            std::make_move_iterator(sub_settings.begin()),
                            std::make_move_iterator(sub_settings.end()));
        }
    }

    return settings;
}

ParameterRegistry::Setting& ParameterRegistry::add_plain_setting(const std::string& prefix,
                                                                 const std::string& name,
                                                                 const json& value,
                                                                 const std::string& whence)
{
    auto pattern = attach_prefix(prefix, name);
    return settings.emplace_back(pattern, value, whence);
}

ParameterRegistry::Setting& ParameterRegistry::add_config_setting(ConfigScopeAction action,
                                                                  const std::string& scope,
                                                                  const std::string& prefix,
                                                                  const std::string& name,
                                                                  const json& value,
                                                                  const std::string& whence,
                                                                  const boost::filesystem::path& from_path)
{
    auto pattern = attach_prefix(prefix, name);
    auto cfg_prefix = scope.empty() ? prefix : attach_prefix(prefix, scope);
    ParameterRegistry* target_registry = nullptr;
    std::unique_ptr<ParameterRegistry> removed_registry;

    std::optional<boost::filesystem::path> value_path;
    if (value.is_string()) {
        value_path.emplace(value.get<std::string>());
        value_path = value_path->lexically_normal();
        if (value_path->is_relative())
            value_path = (from_path / *value_path).lexically_normal();
    }

    decltype(find_cfg_settings(cfg_prefix)) previous;
    if (!scope.empty()) {
        previous = root().find_cfg_settings(cfg_prefix);
        std::unique_ptr<ParameterRegistry>* previous_subregistry = nullptr;
        for (auto [exact, _, setting] : previous) {
            if (exact) {
                previous_subregistry = &setting->subregistry;
                break;
            }
        }
        if (previous_subregistry) {
            if (action == ConfigScopeAction::CREATE
                && (!value_path
                    || std::none_of(previous.begin(),
                                    previous.end(),
                                    [&](const auto& p) {
                                        return std::get<bool>(p)
                                               && std::get<Setting*>(p)->subregistry->previously_assigned_via_config(
                                                   *value_path, whence);
                                    })))
            {
                throw std::runtime_error{"cfg is modifying a config-scope, use 'append-cfg' or 'override-cfg' instead"};
            } else if (action == ConfigScopeAction::OVERRIDE) {
                removed_registry = std::move(*previous_subregistry);
            } else {
                target_registry = previous_subregistry->get();
            }
        } else if (action == ConfigScopeAction::OVERRIDE) {
            throw std::runtime_error{"override-cfg did not override any config-scope"};
        } else if (action == ConfigScopeAction::APPEND) {
            throw std::runtime_error{"append-cfg could not append to an existing config-scope"};
        }
    } else if (action != ConfigScopeAction::CREATE && action != ConfigScopeAction::CREATE_OR_APPEND) {
        throw std::runtime_error{"override-cfg and append-cfg require a non-empty scope"};
    }

    Setting* new_setting = nullptr;
    if (action != ConfigScopeAction::OVERRIDE || !value.is_null()) {
        new_setting = &settings.emplace_back(pattern, value, whence);
        new_setting->matched = true;

        ParameterRegistry* registry = this;
        if (!scope.empty()) {
            auto config_file_context = (!from_path.empty() && value_path) ? whence : "";
            new_setting->subregistry = std::make_unique<ParameterRegistry>(
                cfg_prefix,
                new_setting->is_regex(),
                root(),
                config_file_context,
                removed_registry
                    ? removed_registry.get()
                    : (previous.empty() ? target_registry : std::get<Setting*>(previous.back())->subregistry.get()));

            registry = new_setting->subregistry.get();
            if (target_registry)
                target_registry->cfg_paths.emplace_back(value_path->string());

            cfg_settings.emplace(cfg_prefix, new_setting);
        }

        registry->load_settings(value_path->string(), cfg_prefix);
    }

    if (removed_registry) {
        for (auto* s : std::views::elements<2>(previous))
            s->subregistry.reset();

        auto settings = root().find_cfg_settings(cfg_prefix);
        if (!settings.empty() && (settings.size() > 1 || std::get<Setting*>(settings.front()) != new_setting))
            throw std::runtime_error("Not all config settings were removed");
    }

    return *new_setting;
}

ParameterRegistry::Setting* ParameterRegistry::add_setting(const std::string& pattern,
                                                           const json& value,
                                                           const std::string& whence,
                                                           const boost::filesystem::path& from_path)
{
    return add_setting("", pattern, value, whence, from_path);
}

ParameterRegistry::Setting* ParameterRegistry::add_setting(const std::string& prefix,
                                                           const std::string& name,
                                                           const json& value,
                                                           const std::string& whence,
                                                           const boost::filesystem::path& from_path)
{
    auto [cfg_action, cfg_scope] = extract_cfg_prefix(name);
    if (cfg_action == ConfigScopeAction::INVALID)
        return &add_plain_setting(prefix, name, value, whence);
    return &add_config_setting(cfg_action, cfg_scope, prefix, name, value, whence, from_path);
}

void ParameterRegistry::add_settings(std::initializer_list<std::pair<std::string, json>> setting_list,
                                     const std::string& whence)
{
    for (const auto& [name, value] : setting_list)
        add_setting(name, value, whence);
}

ParameterRegistry::Setting& ParameterRegistry::add_alias(const std::string& pattern,
                                                         const std::string& alias,
                                                         const std::string& whence,
                                                         const boost::filesystem::path& from_path)
{
    return settings.emplace_back(pattern, alias, whence);
}

ParameterRegistry::Parameter& ParameterRegistry::add_parameter_impl(const std::string& name,
                                                                    const std::string& description,
                                                                    const json& default_value,
                                                                    const std::string& default_whence,
                                                                    const ParameterRegistry::Parameter::Type& type)
{
    const auto [setting, aliased] = match_setting_internal(name);
    auto new_param =
        setting ? parameters.emplace(std::piecewise_construct,
                                     std::forward_as_tuple(name),
                                     std::forward_as_tuple(name, description, *setting, type, aliased))
                : parameters.emplace(std::piecewise_construct,
                                     std::forward_as_tuple(name),
                                     std::forward_as_tuple(name, description, default_value, default_whence, type));
    AMPERE_ASSERT_ERROR(new_param.second, "duplicate parameter \"{}\"", name);
    return new_param.first->second;
}

bool ParameterRegistry::within_scope(const std::string& name) const
{
    if (is_regex || scope.empty())
        return true;

    for (auto s = scope.begin(), n = name.begin(); n != name.end(); ++s, ++n) {
        if (s == scope.end())
            return true;
        if (*s != *n)
            return false;
    }

    return false;
}

ParameterRegistry::Setting const* ParameterRegistry::match_setting(const std::string& name)
{
    return match_setting_internal(name).first;
}

std::pair<ParameterRegistry::Setting*, ParameterRegistry::Setting*> ParameterRegistry::match_setting_internal(
    const std::string& name)
{
    auto* setting = match_setting_non_aliased(name);
    Setting* aliased = setting && setting->is_alias() ? setting : nullptr;
    if (aliased) {
        int alias_depth = 1;

        while (setting->is_alias() && alias_depth < 100) {
            const auto& alias_name = setting->alias();
            setting = match_setting_non_aliased(alias_name);

            // Don't allow aliases to fail to match.
            if (!setting)
                AMPERE_ERROR("non-existant alias {}", alias_name);

            ++alias_depth;
        }

        if (alias_depth >= 100)
            AMPERE_ERROR("parameter aliasing too deep (limit 100)");
    }

    return {setting, aliased};
}

ParameterRegistry::Setting* ParameterRegistry::match_setting_non_aliased(const std::string& name, Setting* last_setting)
{
    for (auto& p : settings) {
        if (p.matches(name)) {
            p.matched = true;
            last_setting = (last_setting && p.addendum) ? merge_settings(name, last_setting, &p) : &p;
        }

        if (p.subregistry && p.subregistry->within_scope(name)) {
            if (auto* sub_setting = p.subregistry->match_setting_non_aliased(name, last_setting); sub_setting)
                last_setting = sub_setting;
        }
    }

    return last_setting;
}

static json merge_values(json old_value, json new_value)
{
    json merged(old_value);

    if (old_value.is_array() && new_value.is_array()) {
        for (auto& elem : new_value)
            merged.push_back(elem);
    } else {
        merged.merge_patch(new_value);
    }

    return merged;
}

ParameterRegistry::Setting* ParameterRegistry::merge_settings(const std::string& name,
                                                              Setting* old_setting,
                                                              Setting* new_setting)
{
    std::string whence;

    if (old_setting->whence != new_setting->whence)
        whence = fmt::format("{}/{}", old_setting->whence, new_setting->whence);
    else
        whence = old_setting->whence;

    return &merged_settings
                .insert_or_assign(name,
                                  Setting("<merged>", merge_values(old_setting->value(), new_setting->value()), whence))
                .first->second;
}

class skip_comments : public boost::iostreams::input_filter
{
public:
    template<typename Source>
    int get(Source& src)
    {
        int c;

        while (true) {
            c = boost::iostreams::get(src);

            if (c == EOF || c == boost::iostreams::WOULD_BLOCK)
                break;

            if (c == '#')
                skip = true;
            else if (c == '\n')
                skip = false;

            if (!skip)
                break;
        }

        return c;
    }

    template<typename Source>
    void close(Source&)
    {
        skip = false;
    }

private:
    bool skip{false};
};

void skip_ws(std::istream& is, int& lineno)
{
    while (is) {
        char ch = is.get();

        if (ch == '\n')
            ++lineno;

        if (!(ch == ' ' || ch == '\n' || ch == '\r' || ch == '\t')) {
            is.putback(ch);
            return;
        }
    }
}

/** Expand a variable inside a JSON string. The notation is:
 *
 *      "some text $(variable) blah blah"
 *
 *  Most of the complexity here is dealing with the possibility of a
 *  malformed input. If we don't see the starting sequence "$(", then
 *  just return what we did see literally. And if we run out of input
 *  before seeing the closing ")", that's also an error.
 *
 *  If the variable doesn't exist, then the expansion is successful
 *  but empty.
 */
std::string collect_var(std::istream& is, const std::map<std::string, std::string>& vars)
{
    enum State {
        START,
        FIND_OPEN,
        COLLECT_VAR,
        DONE
    } state{START};
    std::stringstream var;

    while (is && state != DONE) {
        int c = is.get();

        if (c == EOF)
            break;

        char ch = c & 0xff;

        switch (state) {
        case START:
            if (ch == '$')
                state = FIND_OPEN;
            else
                return std::string{ch};
            break;
        case FIND_OPEN:
            if (ch == '(')
                state = COLLECT_VAR;
            else
                return std::string{"$"} + ch;
            break;
        case COLLECT_VAR:
            if (ch == ')')
                state = DONE;
            else
                var << ch;
            break;
        case DONE:
            // does not happen, but makes the compiler happy
            break;
        }
    }

    if (state == DONE) {
        auto it = vars.find(var.str());
        return it == vars.end() ? "" : it->second;
    } else if (state == START) {
        return "";
    } else {
        throw std::runtime_error{"end of input inside variable expansion"};
    }
}

void collect_string(std::istream& is,
                    std::ostringstream& result,
                    int& lineno,
                    const std::map<std::string, std::string>& vars)
{
    char ch = is.get();
    result << ch; // collect initial quote

    while (is) {
        ch = is.get();

        if (ch == '$') {
            is.putback(ch);
            result << collect_var(is, vars);
            continue;
        }

        result << ch;

        if (ch == '\\') {
            if (is) {
                ch = is.get();
                result << ch;
            }
        } else if (ch == '"') {
            break;
        } else if (ch == '\n') {
            ++lineno;
        }
    }
}

void collect_nested_value(char open,
                          char close,
                          std::istream& is,
                          std::ostringstream& result,
                          int& lineno,
                          const std::map<std::string, std::string>& vars)
{
    int nesting = 0;

    while (is) {
        char ch = is.get();

        if (ch == '"') {
            is.putback(ch);
            collect_string(is, result, lineno, vars);
            continue;
        }

        if (ch == open)
            ++nesting;
        else if (ch == close)
            --nesting;
        else if (ch == '\n')
            ++lineno;

        result << ch;

        if (nesting == 0)
            break;
    }
}

#define GETCH(is, ch)     \
    {                     \
        int c = is.get(); \
        if (c == EOF)     \
            return;       \
        ch = c & 0xff;    \
    }

void collect_number(std::istream& is, std::ostringstream& result)
{
    char ch;
    GETCH(is, ch);

    if (ch == '-' || isdigit(ch)) {
        result << ch;
        GETCH(is, ch);

        while (isdigit(ch)) {
            result << ch;
            GETCH(is, ch);
        }

        if (ch == '.') {
            result << ch;
            GETCH(is, ch);

            while (isdigit(ch)) {
                result << ch;
                GETCH(is, ch);
            }
        }

        if (ch == 'e' || ch == 'E') {
            result << ch;
            GETCH(is, ch);

            if (ch == '+' || ch == '-') {
                result << ch;
                GETCH(is, ch);
            }

            while (isdigit(ch)) {
                result << ch;
                GETCH(is, ch);
            }
        }
    }

    is.putback(ch);
}

void collect_nonspace(std::istream& is, std::ostringstream& result, const std::map<std::string, std::string>& vars)
{
    std::ostringstream temp;
    collect_number(is, temp);

    if (temp.tellp() > 0 && (!is || isspace(is.peek()))) {
        result << temp.str();
        return;
    }

    while (true) {
        int c = is.get();

        if (c == EOF)
            break;

        char ch = c & 0xff;

        if (isspace(ch)) {
            is.putback(ch);
            break;
        }

        if (ch == '$') {
            is.putback(ch);
            temp << collect_var(is, vars);
        } else {
            temp << ch;
        }
    }

    auto s = temp.str();

    if (s == "false" || s == "true" || s == "null") {
        result << temp.str();
        return;
    }

    result.put('"');
    result << temp.str();
    result.put('"');
}

std::string collect_json_value(std::istream& is, int& lineno, const std::map<std::string, std::string>& vars)
{
    std::ostringstream result;

    skip_ws(is, lineno);

    if (is) {
        switch (is.peek()) {
        case '{':
            collect_nested_value('{', '}', is, result, lineno, vars);
        case '[':
            collect_nested_value('[', ']', is, result, lineno, vars);
            break;
        case '"':
            collect_string(is, result, lineno, vars);
            break;
        default:
            collect_nonspace(is, result, vars);
            break;
        }
    }

    return result.str();
}

json parse_json_from_value(std::istream& is,
                           int& lineno,
                           const std::map<std::string, std::string>& vars = {},
                           std::optional<boost::filesystem::path> directory = {})
{
    skip_ws(is, lineno);

    if (is.peek() != '@') {
        auto param_string_value = collect_json_value(is, lineno, vars);
        return json::parse(param_string_value);
    }

    is.ignore();
    std::string filename;
    is >> filename;

    auto path = boost::filesystem::path(filename);
    if (path.is_relative() && directory)
        path = (*directory / path);

    std::ifstream json_file(path.string());
    return json::parse(json_file);
}

std::string collect_name(std::istream& is, int& lineno)
{
    std::ostringstream result;

    skip_ws(is, lineno);

    while (is) {
        char ch = is.get();
        if (isspace(ch)) {
            is.putback(ch);
            break;
        }
        result << ch;
    }

    return result.str();
}

void ParameterRegistry::load(const std::string& config_file, const std::string& prefix, ConfigScopeAction action)
{
    add_config_setting(action, prefix, "", prefix, config_file, "command line", {});
}

void ParameterRegistry::load_settings(const std::string& config_file, const std::string& prefix)
{
    auto source = boost::iostreams::file_source(config_file);

    if (!source.is_open())
        AMPERE_ERROR("could not open {}", config_file);

    boost::iostreams::filtering_istream input;
    input.push(skip_comments());
    input.push(source);

    int lineno = 1;

    while (input) {
        try {
            std::string param_name = collect_name(input, lineno);

            if (param_name.empty())
                break;

            auto config_directory = boost::filesystem::path{config_file}.parent_path().lexically_normal();

            if (auto eq_pos = param_name.find('='); eq_pos != std::string::npos) {
                auto alias = param_name.substr(eq_pos + 1);
                param_name = param_name.substr(0, eq_pos);

                add_alias(attach_prefix(prefix, param_name),
                          attach_prefix(prefix, alias),
                          config_file + ":" + std::to_string(lineno),
                          config_directory);
            } else {
                auto param_value = parse_json_from_value(
                    input, lineno, {{"prefix", prefix}, {"cwd", config_directory.string()}}, config_directory);

                add_setting(
                    prefix, param_name, param_value, config_file + ":" + std::to_string(lineno), config_directory);
            }
        } catch (std::exception& e) {
            AMPERE_ERROR("{}:{}: {}", config_file, lineno, e.what());
        }
    }

    cfg_paths.emplace_back(config_file);
}

void ParameterRegistry::load_from_args(int argc, char** argv)
{
    for (int i = 1; i < argc; i += 2) {
        if (argv[i][0] == '-') {
            if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "-help") || !strcmp(argv[i], "--help")) {
                need_help = true;
                return;
            } else if (i < argc - 1) {
                std::string name(argv[i] + 1);
                std::istringstream iss(argv[i + 1]);
                int lineno = 1;
                auto value = parse_json_from_value(iss, lineno);

                add_setting(name, value, "command line");
            } else {
                std::string msg = "missing argument after parameter ";
                msg += argv[i];
                throw std::runtime_error(msg);
            }
        } else {
            AMPERE_ERROR("Argument {} must begin with '-': '{}'", i, argv[i]);
        }
    }
}

bool ParameterRegistry::check_all_matched() const
{
    bool all_matched = true;

    for (const auto& p : settings) {
        if (!p.matched) {
            std::cerr << "Setting " << p.pattern << " was not matched by any parameter\n"
                      << "  defined here: " << p.whence << std::endl;
            all_matched = false;
        }

        if (p.subregistry && !p.subregistry->check_all_matched())
            all_matched = false;
    }

    return all_matched;
}

void ParameterRegistry::dump_parameters(std::ostream& os)
{
    for (auto& p : parameters) {
        Parameter& param = p.second;
        os << param.name << ' ' << param.value() << std::endl;
    }
}

void ParameterRegistry::dump_cfg_settings_verbosely(std::ostream& os, size_t level) const
{
    auto indent = std::string(level * 2, ' ');
    if (!cfg_paths.empty() && level > 0) {
        for (const auto& path : cfg_paths)
            os << indent << path << std::endl;
        os << std::endl;
    }

    std::string previous_name;
    for (const auto& [name, setting] : cfg_settings) {
        if (setting->subregistry) {
            if (previous_name != name)
                os << indent << name << ": " << setting->whence << std::endl;
            setting->subregistry->dump_cfg_settings_verbosely(os, level + 1);
        }

        previous_name = name;
    }
}

void ParameterRegistry::dump_parameters_verbosely(std::ostream& os)
{
    os << "\nParameters:\n";
    for (const auto& param : std::views::values(parameters)) {
        os << param.name << '\n'
           << "  Value: " << param.value() << '\n'
           << "  Description: " << param.description << '\n';

        if (!param.type.name.empty()) {
            os << "  Type: " << param.type.name;
            if (!param.type.specification.empty())
                os << " (" << param.type.specification << ')';
            os << '\n';
        }

        os << "  Whence: " << param.whence();
        if (auto alias = param.aliased(); alias) {
            os << "\n  Aliased to: " << alias->first << "\n          at: " << alias->second;
        }
        os << std::endl;
    }

    if (!cfg_settings.empty()) {
        os << "\nConfig Setting Scopes:\n";
        dump_cfg_settings_verbosely(os);
    }
}

void ParameterRegistry::dump_settings(std::ostream& os)
{
    for (const auto& s : settings) {
        if (s.is_alias())
            os << s.pattern << '=' << s.alias() << " (" << s.whence << ')' << std::endl;
        else
            os << s.pattern << ' ' << s.value() << " (" << s.whence << ')' << std::endl;

        if (s.subregistry)
            s.subregistry->dump_settings(os);
    }
}

void ParameterRegistry::values_to_event_stream()
{
    Environment* env = Environment::get_root();
    for (const auto& param : std::views::values(parameters)) {
        param.type.value_to_event_stream(*env->event_stream, param.name, param.description, param.value());
    }
}

void ParameterRegistry::clear()
{
    settings.clear();
    merged_settings.clear();
    parameters.clear();
    cfg_settings.clear();
}

void ParameterRegistry::Setting::setup_pattern()
{
    if (pattern.empty())
        return;

    if (pattern[0] == '+') {
        addendum = true;
        pattern = pattern.substr(1);
    } else if (pattern[0] == '~') {
        pattern_re.emplace(pattern.substr(1));
    }
}

ParameterRegistry::Setting::Setting(std::string pattern, json value, std::string whence)
    : value_or_alias(std::move(value)), pattern(std::move(pattern)), whence(std::move(whence))
{
    setup_pattern();
}

ParameterRegistry::Setting::Setting(std::string pattern, std::string alias, std::string whence)
    : value_or_alias(std::move(alias)), pattern(std::move(pattern)), whence(std::move(whence))
{
    setup_pattern();
}

} // namespace ampere::sc

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
