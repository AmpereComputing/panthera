//
// Copyright (c) 2018-2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#pragma once

#include <sysc/kernel/sc_time.h>
#include <boost/filesystem.hpp>
#include <climits>
#include <memory>
#include <nlohmann/json.hpp>
#include <limits>
#include <map>
#include <ostream>
#include <regex>
#include <string>
#include <optional>
#include <utility>
#include <variant>
#include <vector>
#include <type_traits>
#include <list>
#include <deque>
#include <set>

#include "ampere/event_stream/event_stream.h"
#include "ampere/sc/poly.h"
#include "ampere/sc/value.h"

#include "ampere/util/enum.h"

namespace ampere::sc {

using json = nlohmann::json;
using ampere::event_stream::EventStream;

namespace parameter_specifications {

void value_to_event_stream_bool(EventStream& event_stream,
                                const std::string& name,
                                const std::string& description,
                                const json& value);
void value_to_event_stream_int(EventStream& event_stream,
                               const std::string& name,
                               const std::string& description,
                               const json& value);
void value_to_event_stream_uint(EventStream& event_stream,
                                const std::string& name,
                                const std::string& description,
                                const json& value);
void value_to_event_stream_double(EventStream& event_stream,
                                  const std::string& name,
                                  const std::string& description,
                                  const json& value);
void value_to_event_stream_string(EventStream& event_stream,
                                  const std::string& name,
                                  const std::string& description,
                                  const json& value);
void value_to_event_stream_json(EventStream& event_stream,
                                const std::string& name,
                                const std::string& description,
                                const json& value);

struct UnknownType
{
    std::string name() const { return {}; }
    std::string specification() const { return {}; }
    static void value_to_event_stream(EventStream& event_stream,
                                      const std::string& name,
                                      const std::string& description,
                                      const json& value)
    {
        if (value.is_string())
            value_to_event_stream_string(event_stream, name, description, value);
        else
            value_to_event_stream_json(event_stream, name, description, value);
    }
};

template<typename T, typename P = void>
struct Type : UnknownType
{
    static constexpr bool is_json_like = std::is_class_v<T> && nlohmann::detail::has_from_json<json, T>::value;

    std::string name() const
    {
        if constexpr (is_json_like) {
            return class_name<T>();
        } else {
            return {};
        }
    }

    std::string specification() const
    {
        if constexpr (is_json_like) {
            return "json-like";
        } else {
            return {};
        }
    }
    static void value_to_event_stream(EventStream& event_stream,
                                      const std::string& name,
                                      const std::string& description,
                                      const json& value)
    {
        value_to_event_stream_json(event_stream, name, description, value);
    }
};

template<typename T>
struct Type<T, std::enable_if_t<std::is_integral_v<T>, void>>
{
    std::string name() const
    {
        if constexpr (std::is_same_v<T, bool>) {
            return "boolean";
        } else if constexpr (std::is_unsigned_v<T>) {
            return "unsigned integer";
        } else {
            return "integer";
        }
    }

    std::string specification() const
    {
        if constexpr (std::is_same_v<T, bool> || std::numeric_limits<T>::digits > (CHAR_BIT * 2)) {
            return {};
        } else {
            return std::to_string(std::numeric_limits<T>::min()) + " - "
                   + std::to_string(std::numeric_limits<T>::max());
        }
    }

    static void value_to_event_stream(EventStream& event_stream,
                                      const std::string& name,
                                      const std::string& description,
                                      const json& value)
    {
        if constexpr (std::is_same_v<T, bool>) {
            value_to_event_stream_bool(event_stream, name, description, value);
        } else if constexpr (std::is_unsigned_v<T>) {
            value_to_event_stream_uint(event_stream, name, description, value);
        } else {
            value_to_event_stream_int(event_stream, name, description, value);
        }
    }
};

template<typename T>
struct Type<T, std::enable_if_t<std::is_floating_point_v<T>, void>> : UnknownType
{
    std::string name() const { return "float"; }
    static void value_to_event_stream(EventStream& event_stream,
                                      const std::string& name,
                                      const std::string& description,
                                      const json& value)
    {
        value_to_event_stream_double(event_stream, name, description, value);
    }
};

template<>
struct Type<std::string, void> : UnknownType
{
    std::string name() const { return "string"; }
    static void value_to_event_stream(EventStream& event_stream,
                                      const std::string& name,
                                      const std::string& description,
                                      const json& value)
    {
        value_to_event_stream_string(event_stream, name, description, value);
    }
};

template<typename T>
struct Type<std::optional<T>, void>
{
    std::string name() const
    {
        if (auto sub_type = Type<T>().name(); !sub_type.empty())
            return "optional " + sub_type;
        return {};
    }
    std::string specification() const { return Type<T>().specification(); }
    static void value_to_event_stream(EventStream& event_stream,
                                      const std::string& name,
                                      const std::string& description,
                                      const json& value)
    {
        value_to_event_stream_json(event_stream, name, description, value);
    }
};

template<typename T>
struct Type<std::vector<T>, std::enable_if_t<!util::is_enumeration_related_v<T> || util::is_constant_enum_v<T>, void>>
{
    std::string name() const
    {
        if (auto sub_type = Type<T>().name(); !sub_type.empty())
            return "list of " + sub_type;
        return {};
    }
    std::string specification() const { return Type<T>().specification(); }
    static void value_to_event_stream(EventStream& event_stream,
                                      const std::string& name,
                                      const std::string& description,
                                      const json& value)
    {
        value_to_event_stream_json(event_stream, name, description, value);
    }
};

template<>
struct Type<sc_core::sc_time, void>
{
    std::string name() const { return "time"; }
    std::string specification() const { return "<time>(s|ms|us|ns|ps|fs)"; }
    static void value_to_event_stream(EventStream& event_stream,
                                      const std::string& name,
                                      const std::string& description,
                                      const json& value)
    {
        value_to_event_stream_string(event_stream, name, description, value);
    }
};

template<>
struct Type<json, void> : UnknownType
{
    std::string name() const { return "json"; }
    static void value_to_event_stream(EventStream& event_stream,
                                      const std::string& name,
                                      const std::string& description,
                                      const json& value)
    {
        value_to_event_stream_json(event_stream, name, description, value);
    }
};

// Specialize parameter usage so help is aware of values
template<typename E>
struct Type<util::Enumeration<E>, std::enable_if_t<util::is_constant_enum_v<E>, void>>
{
    std::string name() const { return "enum"; }
    std::string specification() const
    {
        std::string spec;
        unsigned i = 0;
        for (const auto& value : util::each_enum<E>()) {
            if (i++ > 0)
                spec += ", ";
            spec += util::enum_to_string(value);
        }
        return spec;
    }
    static void value_to_event_stream(EventStream& event_stream,
                                      const std::string& name,
                                      const std::string& description,
                                      const json& value)
    {
        value_to_event_stream_json(event_stream, name, description, value);
    }
};

template<typename E>
struct Type<util::Enumeration<E>, std::enable_if_t<!util::is_constant_enum_v<E>, void>>
{
    util::Enumeration<E> enumeration;
    Type() = default;
    explicit Type(const E& enumeration) : enumeration(enumeration) {}
    explicit Type(const util::Enumeration<E>& enumeration) : enumeration(enumeration) {}

    std::string name() const { return "enum"; }
    std::string specification() const
    {
        if (enumeration.size() == 0)
            return "";

        std::string spec;
        unsigned i = 0;
        for (const auto& [_, value] : enumeration) {
            if (i++ > 0)
                spec += ", ";
            spec += value;
        }
        return spec;
    }
    static void value_to_event_stream(EventStream& event_stream,
                                      const std::string& name,
                                      const std::string& description,
                                      const json& value)
    {
        value_to_event_stream_json(event_stream, name, description, value);
    }
};

template<typename E>
struct Type<std::vector<util::Enumeration<E>>, std::enable_if_t<!util::is_constant_enum_v<E>, void>>
{
    util::Enumeration<E> enumeration;
    Type() = default;
    explicit Type(const E& enumeration) : enumeration(enumeration) {}
    explicit Type(const util::Enumeration<E>& enumeration) : enumeration(enumeration) {}

    std::string name() const
    {
        if (auto sub_type = Type<util::Enumeration<E>>(enumeration).name(); !sub_type.empty())
            return "list of " + sub_type;
        return {};
    }
    std::string specification() const { return Type<E>().specification(); }
    static void value_to_event_stream(EventStream& event_stream,
                                      const std::string& name,
                                      const std::string& description,
                                      const json& value)
    {
        value_to_event_stream_json(event_stream, name, description, value);
    }
};

template<typename E>
struct Type<E, std::enable_if_t<util::has_enum_definition_v<E>, void>> : public Type<util::Enumeration<E>, void>
{};

template<typename T>
struct Type<T, std::enable_if_t<std::is_enum_v<T> && !util::has_enum_definition_v<T>, void>> : UnknownType
{
    std::string name() const { return "enum"; }
};

template<typename T>
struct Type<T, std::enable_if_t<std::is_base_of_v<VerifiedValue, T>, void>>
{
    std::string name() const { return Type<typename T::value_type>().name(); }
    std::string specification() const
    {
        std::string spec;
        if constexpr (!T::specialized_specification) {
            spec += Type<typename T::value_type>().specification();
            if (!spec.empty())
                spec += ", ";
        }
        return spec + T::specification();
    }
    static void value_to_event_stream(EventStream& event_stream,
                                      const std::string& name,
                                      const std::string& description,
                                      const json& value)
    {
        Type<typename T::value_type>::value_to_event_stream(event_stream, name, description, value);
    }
};

} // namespace parameter_specifications

class ParameterRegistry
{
public:
    enum class ConfigScopeAction {
        INVALID,          // Not a config
        CREATE,           // Config will be created (should not exist)
        OVERRIDE,         // Config will override already existing
        APPEND,           // Config will be appended to already existing
        CREATE_OR_APPEND, // Config will be created or appended (if already existing)
    };

    ParameterRegistry() = default;
    ParameterRegistry(const std::string& scope,
                      bool regex,
                      ParameterRegistry& root,
                      const std::string& file_context,
                      ParameterRegistry* previous = nullptr)
        : scope(scope), is_regex(regex), root_registry(&root), previously_used_context({file_context})
    {
        if (previous)
            previously_used_context.insert(previous->previously_used_context.begin(),
                                           previous->previously_used_context.end());
    }
    ParameterRegistry(const ParameterRegistry&) = delete;
    ParameterRegistry(std::initializer_list<std::pair<std::string, json>> settings);

    class Setting
    {
        void setup_pattern();

        std::variant<json, std::string> value_or_alias;

    public:
        Setting(std::string pattern, json value, std::string whence);
        Setting(std::string pattern, std::string alias, std::string whence);

        std::string pattern;
        std::string whence;

        std::optional<std::regex> pattern_re;
        bool matched = false;

        bool addendum = false;

        // The following only applies to "cfg" settings which can load recursively
        // from files.
        std::unique_ptr<ParameterRegistry> subregistry;

        bool is_alias() const { return std::holds_alternative<std::string>(value_or_alias); }
        bool is_regex() const { return static_cast<bool>(pattern_re); }

        const auto& alias() const { return std::get<std::string>(value_or_alias); }
        const auto& value() const { return std::get<json>(value_or_alias); }

        bool matches(const std::string& param) const
        {
            if (pattern_re)
                return std::regex_match(param, *pattern_re);
            return pattern == param;
        }
    };

    class Parameter
    {
        std::unique_ptr<Setting> default_setting;
        Setting const* value_setting{nullptr};
        Setting const* aliased_setting{nullptr};

    public:
        struct Type
        {
            std::string name;
            std::string specification;
            void (*value_to_event_stream)(EventStream& event_stream,
                                          const std::string& name,
                                          const std::string& description,
                                          const json& value);
        };

        Parameter(std::string name, std::string description, const Setting& setting, Type type, const Setting* aliased)
            : value_setting{&setting},
              aliased_setting{aliased},
              name{std::move(name)},
              description{std::move(description)},
              type{std::move(type)}
        {
        }
        Parameter(std::string name, std::string description, json value, std::string whence, Type type)
            : default_setting{std::make_unique<Setting>("", value, whence)},
              value_setting{default_setting.get()},
              name{std::move(name)},
              description{std::move(description)},
              type{std::move(type)}
        {
        }

        std::string name;
        std::string description;
        Type type;

        bool using_default() const { return static_cast<bool>(default_setting); }
        const auto& value() const { return value_setting->value(); };
        const auto& whence() const { return value_setting->whence; };

        std::optional<std::pair<std::string, std::string>> aliased() const
        {
            if (aliased_setting)
                return std::make_pair(value_setting->pattern, aliased_setting->whence);
            return {};
        }
    };

    Setting* add_setting(const std::string& pattern,
                         const json& value,
                         const std::string& whence,
                         const boost::filesystem::path& from_path = {});
    Setting* add_setting(const std::string& prefix,
                         const std::string& name,
                         const json& value,
                         const std::string& whence,
                         const boost::filesystem::path& from_path = {});
    void add_settings(std::initializer_list<std::pair<std::string, json>> settings, const std::string& whence);

    Setting& add_alias(const std::string& pattern,
                       const std::string& alias,
                       const std::string& whence,
                       const boost::filesystem::path& from_path = {});

    template<typename T>
    Parameter& add_parameter_with_type(const std::string& name,
                                       const std::string& description,
                                       const json& default_value,
                                       const std::string& default_whence,
                                       const parameter_specifications::Type<T>& spec = {})
    {
        return add_parameter_impl(name,
                                  description,
                                  default_value,
                                  default_whence,
                                  Parameter::Type{
                                      spec.name(),
                                      spec.specification(),
                                      &spec.value_to_event_stream,
                                  });
    }

    bool within_scope(const std::string& name) const;
    Setting const* match_setting(const std::string& name);

    void load(const std::string& config_file,
              const std::string& prefix = "",
              ConfigScopeAction action = ConfigScopeAction::CREATE_OR_APPEND);
    void load_from_args(int argc, char** argv);

    void clear();

    bool check_all_matched() const;
    void dump_parameters(std::ostream&);
    void dump_parameters_verbosely(std::ostream&);
    void dump_settings(std::ostream&);
    void values_to_event_stream();

    bool help_requested() const { return need_help; }

protected:
    std::string scope;
    bool is_regex = false;
    ParameterRegistry* root_registry = nullptr;

    std::deque<Setting> settings;
    std::map<std::string, Setting> merged_settings;
    std::map<std::string, Parameter> parameters;
    std::multimap<std::string, Setting*> cfg_settings;
    std::list<std::string> cfg_paths;
    std::set<std::string> previously_used_context;
    bool need_help = false;

    const std::string& path() const { return cfg_paths.back(); }
    bool single_path_only() const { return cfg_paths.size() == 1; }
    bool previously_assigned_via_config(const boost::filesystem::path& path, const std::string& whence) const
    {
        return (single_path_only() && this->path() == path)
               || previously_used_context.find(whence) != previously_used_context.end();
    }

    void load_settings(const std::string& config_file, const std::string& prefix = "");
    Setting& add_plain_setting(const std::string& prefix,
                               const std::string& name,
                               const json& value,
                               const std::string& whence);
    Setting& add_config_setting(ConfigScopeAction action,
                                const std::string& scope,
                                const std::string& prefix,
                                const std::string& name,
                                const json& value,
                                const std::string& whence,
                                const boost::filesystem::path& from_path);

    std::pair<Setting*, Setting*> match_setting_internal(const std::string& name);
    Setting* match_setting_non_aliased(const std::string& name, Setting* last_setting = nullptr);
    Setting* merge_settings(const std::string& name, Setting* old_setting, Setting* new_setting);

    ParameterRegistry& root() { return root_registry ? *root_registry : *this; }
    std::vector<std::tuple<bool, ParameterRegistry*, ParameterRegistry::Setting*>> find_cfg_settings(
        const std::string& prefix);
    void dump_cfg_settings_verbosely(std::ostream&, size_t level = 0) const;

    Parameter& add_parameter_impl(const std::string& name,
                                  const std::string& description,
                                  const json& default_value,
                                  const std::string& default_whence,
                                  const Parameter::Type& type);
};

} // namespace ampere::sc

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
