//
// Copyright (c) 2018-2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#pragma once

#include <fstream>
#include <fmt/base.h>
#include <fmt/format.h>
#include <google/protobuf/extension_set.h>
#include <stdexcept>
#include <string>
#include <systemc>
#include <type_traits>
#include <optional>
#include <utility>
#include <vector>

#include "ampere/sc/ancestor.h"
#include "ampere/sc/environment.h"
#include "ampere/sc/param_registry.h"
#include "ampere/sc/value.h"
#include "ampere/sc/module.h"

#include "ampere/util/enum.h"

namespace ampere::sc {

template<typename T, typename P = void>
struct ParamJSONDeserializer;

template<typename T, typename Deserializer = ParamJSONDeserializer<T>>
class _Param : public sc_core::sc_object
{
public:
    using value_type = T;
    using reference_type = param_value_reference_type<T>;
    using pointer_type = const T*;

    template<typename U = json>
    _Param(const std::string& name,
           const std::string& description,
           const U& def,
           const parameter_specifications::Type<T>& spec = {},
           Deserializer* deserializer = nullptr);
    template<typename U = json>
    _Param(Environment& env,
           const std::string& name,
           const std::string& description,
           const U& def,
           const parameter_specifications::Type<T>& spec = {},
           Deserializer* deserializer = nullptr);
    _Param(const _Param&) = delete;

    const T& val() const { return value; }

    const std::string& description() const { return parameter->description; }
    const std::string& whence() const { return parameter->whence(); };

protected:
    ParameterRegistry::Parameter* parameter;
    T value;

    template<typename U>
    ParameterRegistry::Parameter& init_parameter(const std::string& description,
                                                 const U& def,
                                                 const parameter_specifications::Type<T>& spec = {});
    template<typename U>
    ParameterRegistry::Parameter& init_parameter(Environment& env,
                                                 const std::string& description,
                                                 const U& def,
                                                 const parameter_specifications::Type<T>& spec = {});

    void construct_parameter(Deserializer* deserializer = nullptr);
};

template<typename Deserializer>
struct _ParamDeserializerMixin
{
    template<typename... Args>
    _ParamDeserializerMixin(Args&&... args) : _deserializer(std::forward<Args>(args)...)
    {
    }

    Deserializer _deserializer;
};

template<typename T, typename P = void>
class Param : public _Param<T>
{
    using Base = _Param<T>;

public:
    using Base::Base;

    operator typename Base::reference_type() const { return Base::value; }
    Base::reference_type operator*() const { return Base::value; }
    Base::pointer_type operator->() const { return &this->value; }
};

template<typename T>
class Param<T, std::enable_if_t<std::is_base_of_v<VerifiedValue, T>>> : public _Param<T>
{
    using Base = _Param<T>;
    using underlying_type = T::value_type;
    using underlying_reference_type = param_value_reference_type<underlying_type>;
    using underlying_pointer_type = const underlying_type*;

public:
    using Base::Base;

    operator underlying_reference_type() const { return static_cast<underlying_reference_type>(Base::value); }
    decltype(auto) operator*() const { return static_cast<underlying_reference_type>(Base::value); }
    auto operator->() const { return &static_cast<underlying_reference_type>(Base::value); }
};

template<typename T>
class Param<T, decltype(std::declval<T>().operator->(), void())> : public _Param<T>
{
    using Base = _Param<T>;

public:
    using Base::Base;

    explicit operator bool() const { return static_cast<bool>(Base::value); }
    bool has_value() const { return static_cast<bool>(Base::value); }

    decltype(auto) operator*() const { return *Base::value; }
    auto operator->() const { return &*Base::value; }

    template<typename U>
    bool operator==(const U& other) const
    {
        return Base::value == other;
    }
    template<typename U>
    bool operator!=(const U& other) const
    {
        return Base::value != other;
    }

    template<typename U>
    auto value_or(U&& or_value) const -> std::remove_cvref_t<decltype(*Base::value)>
    {
        if (*this)
            return *Base::value;
        return std::forward<U>(or_value);
    }
};

template<typename E, typename T>
class _EnumParam : protected _ParamDeserializerMixin<ParamJSONDeserializer<T>>, public _Param<T>
{
protected:
    using enum_t = E;
    using full_enum_t = util::Enumeration<enum_t>;
    using Deserializer = _ParamDeserializerMixin<ParamJSONDeserializer<T>>;
    using Base = _Param<T>;

public:
    template<typename U = json>
    _EnumParam(const std::string& name, const std::string& description, const U& def, const enum_t& enumeration)
        : Deserializer(enumeration),
          Base(name, description, def, parameter_specifications::Type<T>(enumeration), &this->_deserializer)
    {
    }
    template<typename U = json>
    _EnumParam(Environment& env,
               const std::string& name,
               const std::string& description,
               const U& def,
               const enum_t& enumeration)
        : Deserializer(enumeration),
          Base(env, name, description, def, parameter_specifications::Type<T>(enumeration), &this->_deserializer)
    {
    }
};

template<typename T, size_t MAX_SIZE>
class Param<util::FrozenEnumeration<T, MAX_SIZE>>
    : public _EnumParam<util::FrozenEnumeration<T, MAX_SIZE>, util::Enumeration<util::FrozenEnumeration<T, MAX_SIZE>>>
{
    using Base =
        _EnumParam<util::FrozenEnumeration<T, MAX_SIZE>, util::Enumeration<util::FrozenEnumeration<T, MAX_SIZE>>>;

public:
    using Base::Base;

    operator const typename Base::full_enum_t&() const { return Base::value; }

    const Base::full_enum_t& operator*() const { return Base::value; }
    const Base::full_enum_t* operator->() const { return &this->value; }
};

template<typename T, size_t MAX_SIZE>
class Param<std::vector<util::FrozenEnumeration<T, MAX_SIZE>>>
    : public _EnumParam<util::FrozenEnumeration<T, MAX_SIZE>,
                        std::vector<util::Enumeration<util::FrozenEnumeration<T, MAX_SIZE>>>>
{
    using Value = std::vector<util::Enumeration<util::FrozenEnumeration<T, MAX_SIZE>>>;
    using Base = _EnumParam<util::FrozenEnumeration<T, MAX_SIZE>, Value>;

public:
    using Base::Base;

    operator const Value&() const { return Base::value; }

    const Value& operator*() const { return Base::value; }
    const Value* operator->() const { return &this->value; }
};

template<typename T, typename P>
struct ParamJSONDeserializer
{
    T operator()(const json& jval) { return jval.get<T>(); }
};

template<typename T>
inline static T from_json_T(const json& jval)
{
    return ParamJSONDeserializer<T>()(jval);
}

namespace {

template<typename Int>
Int check_signed_and_cast(long long int val)
{
    static_assert(std::is_signed_v<Int>);

    if (val > std::numeric_limits<Int>::max() || val < std::numeric_limits<Int>::min())
        throw std::out_of_range{"signed integer"};

    return static_cast<Int>(val);
}

template<typename Int>
Int string_to_signed_integer(const std::string& s)
{
    static_assert(std::is_signed_v<Int>);

    const char* sc = s.c_str();
    char* endp = nullptr;

    errno = 0;
    long long int x = std::strtoll(sc, &endp, 0);

    if (errno == ERANGE)
        throw std::out_of_range{"string to signed integer"};
    else if (endp != sc + s.size())
        throw std::invalid_argument{"string to signed integer"};

    return check_signed_and_cast<Int>(x);
}

template<typename UInt>
UInt check_unsigned_and_cast(unsigned long long int val)
{
    static_assert(std::is_unsigned_v<UInt>);

    if (val > std::numeric_limits<UInt>::max() || val < std::numeric_limits<UInt>::min())
        throw std::out_of_range{"unsigned integer"};

    return static_cast<UInt>(val);
}

template<typename UInt>
UInt string_to_unsigned_integer(const std::string& s)
{
    static_assert(std::is_unsigned_v<UInt>);

    const char* sc = s.c_str();
    char* endp = nullptr;

    errno = 0;
    unsigned long long int x = std::strtoull(sc, &endp, 0);

    if (errno == ERANGE)
        throw std::out_of_range{"string to unsigned integer"};
    else if (endp != sc + s.size())
        throw std::invalid_argument{"string to unsigned integer"};

    return check_unsigned_and_cast<UInt>(x);
}

} // namespace

template<typename T, typename P = void>
struct ParamJSONSerializer
{
    json operator()(const T& v) { return json(v); }
};

template<typename T>
inline static json to_json_T(const T& v)
{
    return ParamJSONSerializer<T>()(v);
}

template<>
struct ParamJSONDeserializer<bool>
{
    bool operator()(const json& jval)
    {
        if (jval.is_string()) {
            auto s_lower = jval.get<std::string>();
            std::transform(s_lower.begin(), s_lower.end(), s_lower.begin(), ::tolower);

            if (s_lower == "y" || s_lower == "yes" || s_lower == "true" || s_lower == "on") {
                return true;
            } else if (s_lower == "n" || s_lower == "no" || s_lower == "false" || s_lower == "off") {
                return false;
            } else {
                try {
                    return std::stoi(s_lower) != 0;
                } catch (std::invalid_argument&) {
                    throw std::invalid_argument{"badly formatted bool parameter"};
                }
            }
        } else if (jval.is_number_unsigned()) {
            return jval.get<unsigned>() != 0;
        } else {
            return jval.get<bool>();
        }
    }
};

template<typename T>
struct ParamJSONDeserializer<T, std::enable_if_t<std::is_integral_v<T> && std::is_signed_v<T>, void>>
{
    T operator()(const json& jval)
    {
        if (jval.is_string())
            return string_to_signed_integer<T>(jval.get<std::string>());
        else
            return check_signed_and_cast<T>(jval.get<long long>());
    }
};

template<typename T>
struct ParamJSONDeserializer<
    T,
    std::enable_if_t<std::is_integral_v<T> && std::is_unsigned_v<T> && !std::is_same_v<T, bool>, void>>
{
    T operator()(const json& jval)
    {
        if (jval.is_string())
            return string_to_unsigned_integer<T>(jval.get<std::string>());
        else
            return check_unsigned_and_cast<T>(jval.get<unsigned long long>());
    }
};

template<typename T>
struct ParamJSONDeserializer<T, std::enable_if_t<std::is_floating_point_v<T>, void>>
{
    T operator()(const json& jval)
    {
        if (jval.is_string()) {
            if constexpr (std::is_same_v<T, float>) {
                return std::stof(jval.get<std::string>());
            } else if constexpr (std::is_same_v<T, double>) {
                return std::stod(jval.get<std::string>());
            } else if constexpr (std::is_same_v<T, long double>) {
                return std::stold(jval.get<std::string>());
            }
        } else {
            return jval.get<T>();
        }
    }
};

template<>
struct ParamJSONDeserializer<sc_core::sc_time>
{
    sc_core::sc_time operator()(const json& jval)
    {
        using namespace std::string_literals;
        std::map<std::string, sc_core::sc_time_unit> suffix_to_unit = {
            {"fs"s, sc_core::SC_FS},
            {"ps"s, sc_core::SC_PS},
            {"ns"s, sc_core::SC_NS},
            {"us"s, sc_core::SC_US},
            {"ms"s, sc_core::SC_MS},
            {"s"s, sc_core::SC_SEC},
        };

        std::stringstream ss(jval.get<std::string>());
        ss.exceptions(std::ifstream::failbit);
        double value;
        std::string suffix;

        try {
            ss >> value >> suffix;
            return sc_core::sc_time{value, suffix_to_unit.at(suffix)};
        } catch (std::ios_base::failure&) {
            throw std::invalid_argument{"badly formatted time parameter"};
        } catch (std::out_of_range&) {
            throw std::invalid_argument{std::string{"unrecognized time suffix "} + suffix};
        }
    }
};

template<>
struct ParamJSONSerializer<sc_core::sc_time, void>
{
    json operator()(const sc_core::sc_time& v) { return json(v.to_string()); }
};

template<typename T>
struct ParamJSONDeserializer<std::optional<T>>
{
    std::optional<T> operator()(const json& jval)
    {
        if (jval.is_null())
            return std::nullopt;
        return ParamJSONDeserializer<T>()(jval);
    }
};

template<typename T>
struct ParamJSONSerializer<std::optional<T>, void>
{
    json operator()(const std::optional<T>& v)
    {
        if (!v)
            return nullptr;
        return ParamJSONSerializer<T>()(*v);
    }
};

template<typename T>
struct ParamJSONDeserializer<std::unique_ptr<T>>
{
    std::unique_ptr<T> operator()(const json& jval)
    {
        if (jval.is_null())
            return nullptr;
        return std::make_unique<T>(ParamJSONDeserializer<T>()(jval));
    }
};

template<typename E>
struct _ParamJSONEnumDeserializer
{
    E const* enumeration = nullptr;
    _ParamJSONEnumDeserializer() = default;
    explicit _ParamJSONEnumDeserializer(const E& enumeration) : enumeration(&enumeration) {}

    auto operator()(const json& jval)
    {
        if (!enumeration)
            throw std::logic_error{"No enumeration definition provided"};
        util::Enumeration<E> e(*enumeration);
        jval.get_to(e);
        return e;
    }
};

template<typename T, size_t MAX_SIZE>
class ParamJSONDeserializer<util::Enumeration<util::FrozenEnumeration<T, MAX_SIZE>>>
    : public _ParamJSONEnumDeserializer<util::FrozenEnumeration<T, MAX_SIZE>>
{
    using Base = _ParamJSONEnumDeserializer<util::FrozenEnumeration<T, MAX_SIZE>>;

public:
    using Base::Base;
};

template<typename T, size_t MAX_SIZE>
class ParamJSONDeserializer<std::vector<util::Enumeration<util::FrozenEnumeration<T, MAX_SIZE>>>>
    : public _ParamJSONEnumDeserializer<util::FrozenEnumeration<T, MAX_SIZE>>
{
    using enum_t = util::FrozenEnumeration<T, MAX_SIZE>;
    using Value = util::Enumeration<enum_t>;
    using Base = _ParamJSONEnumDeserializer<enum_t>;

public:
    using Base::Base;

    auto operator()(const json& jval)
    {
        std::vector<Value> values;
        for (const auto& j : jval)
            values.emplace_back(Base::operator()(j));
        return values;
    }
};

template<typename T>
struct ParamJSONSerializer<std::unique_ptr<T>, void>
{
    json operator()(const std::unique_ptr<T>& v)
    {
        if (!v)
            return nullptr;
        return ParamJSONSerializer<T>()(*v);
    }
};

template<typename T>
struct ParamJSONSerializer<util::Enumeration<T>, void>
{
    json operator()(const util::Enumeration<T>& v) { return static_cast<std::string_view>(v); }
};

template<typename T>
struct ParamJSONSerializer<T, std::enable_if_t<util::has_enum_definition_v<T>, void>>
{
    json operator()(const T& v) { return util::enum_to_string<T>(v); }
};

template<typename T>
struct ParamJSONDeserializer<T, std::enable_if_t<util::has_enum_definition_v<T>, void>>
{
    T operator()(const json& jval)
    {
        auto converted = ::ampere::util::Enumeration<T>();
        from_json(jval, converted);
        return converted;
    }
};

template<typename T>
struct ParamJSONDeserializer<T, std::enable_if_t<std::is_base_of_v<VerifiedValue, T>, void>>
{
    T operator()(const json& jval) { return ParamJSONDeserializer<typename T::value_type>()(jval); }
};

template<typename T, typename Deserializer>
template<typename U>
_Param<T, Deserializer>::_Param(const std::string& name,
                                const std::string& description,
                                const U& def,
                                const parameter_specifications::Type<T>& spec,
                                Deserializer* deserializer)
    : sc_core::sc_object(name.c_str()), parameter(&init_parameter(description, def, spec))
{
    construct_parameter(deserializer);
}

template<typename T, typename Deserializer>
template<typename U>
_Param<T, Deserializer>::_Param(Environment& env,
                                const std::string& name,
                                const std::string& description,
                                const U& def,
                                const parameter_specifications::Type<T>& spec,
                                Deserializer* deserializer)
    : sc_core::sc_object(name.c_str()), parameter(&init_parameter(env, description, def, spec))
{
    construct_parameter(deserializer);
}

template<typename T, typename Deserializer>
template<typename U>
ParameterRegistry::Parameter& _Param<T, Deserializer>::init_parameter(const std::string& description,
                                                                      const U& def,
                                                                      const parameter_specifications::Type<T>& spec)
{
    auto* module = get_nearest<Module>();
    if (!module)
        throw std::runtime_error{"Param declared outside module without explicit environment"};
    auto& registry = module->get_environment()->parameters;
    auto x = ParamJSONSerializer<U>()(def);
    return registry.add_parameter_with_type<T>(name(), description, x, "default", spec);
}

template<typename T, typename Deserializer>
template<typename U>
ParameterRegistry::Parameter& _Param<T, Deserializer>::init_parameter(Environment& env,
                                                                      const std::string& description,
                                                                      const U& def,
                                                                      const parameter_specifications::Type<T>& spec)
{
    return env.parameters.add_parameter_with_type<T>(
        name(), description, ParamJSONSerializer<U>()(def), "default", spec);
}

template<typename T, typename Deserializer>
void _Param<T, Deserializer>::construct_parameter(Deserializer* deserializer)
{
    try {
        value = deserializer ? (*deserializer)(parameter->value()) : Deserializer()(parameter->value());
    } catch (json::exception& e) {
        throw std::invalid_argument{fmt::format("bad value of '{}' for parameter {} from {}: {}",
                                                parameter->value().dump(),
                                                this->name(),
                                                parameter->whence(),
                                                e.what())};
    } catch (std::invalid_argument& e) {
        throw std::invalid_argument{fmt::format("bad value of '{}' for parameter {} from {}: {}",
                                                parameter->value().dump(),
                                                this->name(),
                                                parameter->whence(),
                                                e.what())};
    }
}

} // namespace ampere::sc

template<typename T>
struct fmt::formatter<ampere::sc::Param<T>> : fmt::formatter<T>
{
    template<typename FormatContext>
    constexpr auto format(const ampere::sc::Param<T>& param, FormatContext& ctx) const
    {
        return fmt::formatter<T>::format(param.val(), ctx);
    }
};

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
