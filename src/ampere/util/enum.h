// Copyright (c) 2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#pragma once

#include <concepts>
#include <cstddef>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <regex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <ostream>
#include <type_traits>

#include <nlohmann/json.hpp>
#include <utility>
#include <fmt/core.h>
#include <fmt/format.h>

#include "ampere/util/arrays.h"  // IWYU pragma: keep
#include "ampere/util/strings.h" // IWYU pragma: keep
#include "ampere/util/bits.h"    // IWYU pragma: keep

using namespace std::literals;

namespace ampere::util {

using nlohmann::json;

// Enumeration definition which make_enum specializes to provide string definitions
// otherwise it indicates that enum has no strings defined
template<class E>
class EnumerationDefinition
{
public:
    using value_type = E;
    using underlying_type = E;
    // Enumeration definition exists
    static constexpr bool exists = false;
    // Enumeration is constant (i.e. constexpr)
    static constexpr bool constant = false;
    // Enumeration is signular (i.e. has one global, singular set of values)
    static constexpr bool singular = false;
};

template<typename T>
concept ConstEnumerationT = std::is_enum_v<T> && EnumerationDefinition<T>::exists;
template<typename T>
concept ConstEnumerationWithValuesT = ConstEnumerationT<T> && EnumerationDefinition<T>::has_values;

template<ConstEnumerationT E>
class ConstEnumIterator
{
    using iterator_t = std::underlying_type_t<E>;
    iterator_t it;

public:
    using difference_type = std::make_signed_t<std::underlying_type_t<E>>;
    using value_type = const E;

    constexpr ConstEnumIterator() = default;
    constexpr explicit ConstEnumIterator(iterator_t it) : it(it) {}

    inline static constexpr auto begin() { return ConstEnumIterator(); }
    inline static constexpr auto end()
    {
        return ConstEnumIterator(static_cast<iterator_t>(EnumerationDefinition<E>::strings.size()));
    }

    constexpr value_type operator*() const { return static_cast<value_type>(it); }
    constexpr ConstEnumIterator &operator++()
    {
        ++it;
        return *this;
    }
    constexpr ConstEnumIterator operator++(int)
    {
        auto incr = *this;
        ++*this;
        return incr;
    }

    constexpr bool operator==(const ConstEnumIterator &other) const { return it == other.it; }
};

template<ConstEnumerationWithValuesT E>
class ConstEnumIterator<E>
{
    using definition_type = EnumerationDefinition<E>;
    using iterator_t = typename decltype(definition_type::strings)::const_iterator;
    iterator_t it;

public:
    using difference_type = std::make_signed_t<std::underlying_type_t<E>>;
    using value_type = const E;

    constexpr ConstEnumIterator() = default;
    constexpr explicit ConstEnumIterator(iterator_t it) : it(it) {}

    inline static constexpr auto begin() { return ConstEnumIterator(definition_type::strings.begin()); }
    inline static constexpr auto end() { return ConstEnumIterator(definition_type::strings.end()); }

    constexpr value_type operator*() const { return static_cast<value_type>(it->second); }
    constexpr ConstEnumIterator &operator++()
    {
        ++it;
        return *this;
    }
    constexpr ConstEnumIterator operator++(int)
    {
        auto incr = *this;
        ++*this;
        return incr;
    }

    constexpr bool operator==(const ConstEnumIterator &other) const { return it == other.it; }
};

template<typename BasicJsonType>
inline std::optional<size_t> _try_from_numeric_json(const BasicJsonType &j)
{
    static const std::regex integer_re{R"( *[-+]?[0-9]+)"};
    if (j.is_number())
        return j.template get<size_t>();

    auto value = j.template get<std::string>();
    if (std::regex_match(value, integer_re))
        return std::stoull(value);

    return {};
}

template<class E>
struct _Enumeration
{
    friend std::ostream &operator<<(std::ostream &os, const E &enumeration)
    {
        return os << static_cast<std::string_view>(enumeration);
    }

    template<typename BasicJsonType>
    static E _from_json(const BasicJsonType &j)
    {
        if (auto number = _try_from_numeric_json(j); number)
            return E(*number);
        return E(j.template get<std::string>());
    }

    template<typename BasicJsonType>
    friend void from_json(const BasicJsonType &j, E &enumeration)
    {
        enumeration = _from_json(j);
    }
};

// Enumeration wrapper (if defined), providing method-based accessors to enumeration attributes and serialization
// Used by plain enum functions (below) and serialization specializations.
template<class E>
class Enumeration : public _Enumeration<Enumeration<E>>, public EnumerationDefinition<E>
{
    using definition_type = EnumerationDefinition<E>;
    typename definition_type::value_type _value;

public:
    using enumeration_type = E;
    using value_type = typename definition_type::value_type;
    using underlying_type = typename definition_type::underlying_type;

    Enumeration() = default;
    constexpr explicit Enumeration(value_type value) : _value(value) {}
    explicit Enumeration(size_t value) : _value(static_cast<value_type>(value)) {}
    explicit Enumeration(std::string_view name)
    {
        for (const auto &[enum_name, enum_value] : definition_type::strings) {
            if (enum_name == name) {
                _value = static_cast<value_type>(enum_value);
                return;
            }
        }

        throw std::out_of_range(fmt::format("No enum value for name: {}", name));
    }

    inline static constexpr size_t size() { return definition_type::strings.size(); }
    inline static constexpr size_t max_size() { return definition_type::max_size + definition_type::offset + 1; }

    constexpr value_type value() const { return _value; }
    constexpr underlying_type underlying() const { return static_cast<underlying_type>(_value); }
    constexpr std::string_view name() const
    {
        if constexpr (!definition_type::has_values) {
            return definition_type::strings.at(static_cast<size_t>(_value)).first;
        } else if constexpr (definition_type::uniformity != RangeUniformity::NONE) {
            return definition_type::strings_by_value.at(
                exploded_index<definition_type::uniformity, underlying_type, definition_type::offset>(
                    std::to_underlying(_value)));
        } else {
            for (const auto &[enum_name, enum_value] : definition_type::strings)
                if (enum_value == std::to_underlying(_value))
                    return enum_name;

            throw std::out_of_range(fmt::format("No enum name for value: {}", static_cast<underlying_type>(_value)));
        }
    }

    constexpr operator value_type() const { return value(); }
    constexpr explicit operator std::string_view() const { return name(); }

    bool operator==(const Enumeration &other) const { return _value == other._value; }
    bool operator!=(const Enumeration &other) const { return _value != other._value; }
    bool operator==(value_type other) const { return _value == other; }
    bool operator!=(value_type other) const { return _value != other; }

    inline static constexpr auto &string_to_value_mapping() { return definition_type::strings; }

    using iterator = ConstEnumIterator<E>;
    static_assert(std::forward_iterator<iterator>);

    constexpr iterator begin() const { return iterator::begin(); }
    constexpr iterator end() const { return iterator::end(); }
};

using dynamic_enum_default_t = int;

template<typename T, size_t MAX_SIZE>
class FrozenEnumeration;

// Const Dynamic Enumeration, read-only interface to a dynamic (non-constant-time) enumeration
template<typename T = dynamic_enum_default_t, size_t MAX_SIZE = std::numeric_limits<T>::max()>
class ConstDynamicEnumeration
{
    static_assert(std::is_integral_v<T>, "Underlying type should be an integer type");

protected:
    std::map<T, std::string> values;
    using value_iterator = typename decltype(values)::const_iterator;

public:
    using value_type = T;

    class Value
    {
        std::optional<value_iterator> iter;
        explicit Value(value_iterator iter) : iter(iter) {}

        friend ConstDynamicEnumeration<T, MAX_SIZE>;

    public:
        Value() = default;
        explicit operator value_type() const { return (*iter)->first; }
        explicit operator const std::string &() const { return (*iter)->second; }
        explicit operator std::string_view() const { return (*iter)->second; }

        friend std::ostream &operator<<(std::ostream &os, const Value &value)
        {
            return os << static_cast<std::string>(value);
        }

        bool operator==(const Value &other) const { return iter == other.iter; }
        bool operator!=(const Value &other) const { return iter != other.iter; }
    };

    inline static bool bounded() { return MAX_SIZE != std::numeric_limits<T>::max(); }
    inline static constexpr auto max_size() { return MAX_SIZE; }

    bool empty() const { return values.empty(); }
    auto size() const { return values.size(); }
    const auto &mapping() const { return values; }

    auto at(value_type value) const
    {
        if (auto iter = values.find(value); iter != values.end())
            return Value(iter);
        throw std::out_of_range("No enumeration for value");
    }
    auto at(const std::string &name) const
    {
        if (auto iter = std::find_if(values.begin(), values.end(), [&](const auto &e) { return e.second == name; });
            iter != values.end())
            return Value(iter);
        throw std::out_of_range(fmt::format("No enumeration for name: {}", name));
    }

    const std::string &to_string(value_type value) const { return values.at(value); }

    auto begin() const { return values.cbegin(); }
    auto end() const { return values.cend(); }
};

template<typename T>
concept ConstDynamicEnumerationT = std::derived_from<T, ConstDynamicEnumeration<typename T::value_type, T::max_size()>>;

template<typename T, typename U>
concept SimilarEnumerationT = ConstEnumerationT<T> || std::constructible_from<U, typename T::underlying_type>;

template<typename T>
concept NonConstDynamicEnumerationT = T::exists && !T::constant;

template<typename T, typename U>
concept ConstructibleEnumerationT = ConstDynamicEnumerationT<T> && std::constructible_from<U, typename T::value_type>;

// Dynamic enumeration, an enumeration that is non-constant-time and can grow/change.
// Also used for building frozen/static enumerations.
template<typename T = dynamic_enum_default_t, size_t MAX_SIZE = std::numeric_limits<T>::max()>
class DynamicEnumeration : public ConstDynamicEnumeration<T, MAX_SIZE>
{
    template<typename V, size_t M>
    friend class FrozenEnumeration;

    template<typename U, typename V, size_t M>
    friend class StaticEnumeration;

public:
    using value_type = T;

    DynamicEnumeration() = default;
    template<SimilarEnumerationT<T> E>
    explicit DynamicEnumeration(std::optional<E> from, std::optional<E> to = {})
    {
        extend<E>(from, to);
    }
    template<ConstructibleEnumerationT<T> U>
    explicit DynamicEnumeration(const Enumeration<U> &enumeration,
                                bool with_values,
                                std::optional<Enumeration<U>> from,
                                std::optional<Enumeration<U>> to)
    {
        extend(enumeration, with_values, from, to);
    }

    value_type add(const std::string &name, value_type value)
    {
        if (value > static_cast<T>(0) && static_cast<size_t>(value) >= MAX_SIZE)
            throw std::logic_error("Value outside max");
        if (bool added = this->values.emplace(value, name).second; !added)
            throw std::logic_error("Enum value already added");
        return value;
    }

    value_type add(const std::string &name)
    {
        value_type value = this->values.empty() ? 0 : this->values.rbegin()->first + 1;
        if (std::find_if(this->values.begin(), this->values.end(), [&](const auto &e) { return e.second == name; })
            != this->values.end())
            throw std::logic_error("Enum value name already added");
        return add(name, value);
    }

private:
    void add(const std::string &name, value_type value, bool with_value)
    {
        if (with_value)
            add(name, value);
        else
            add(name);
    }

    template<ConstEnumerationT E>
    void extend(bool with_values, std::optional<E> from, std::optional<E> to, std::string prefix)
    {
        bool started = !from;
        const auto &mappings = Enumeration<E>::string_to_value_mapping();
        for (const auto &[enum_name, enum_value] : mappings) {
            started = started || enum_value == std::to_underlying(*from);
            if (started) {
                add(prefix + std::string(enum_name), static_cast<value_type>(enum_value), with_values);
                if (to && enum_value == std::to_underlying(*to))
                    break;
            }
        }
    }

    template<NonConstDynamicEnumerationT E>
    void extend(bool with_values, std::optional<E> from, std::optional<E> to, std::string prefix)
    {
        bool started = !from;
        const auto &mappings = E::string_to_value_mapping();
        for (const auto &[enum_value, enum_name] : mappings) {
            started = started || enum_value == from->underlying();
            if (started) {
                add(prefix + enum_name, enum_value, with_values);
                if (to && enum_value == to->underlying())
                    break;
            }
        }
    }

    template<typename U, size_t S>
    void extend_all(bool with_values, const ConstDynamicEnumeration<U, S> &e, std::string prefix)
    {
        for (const auto &[enum_value, enum_name] : e.mapping())
            add(prefix + enum_name, enum_value, with_values);
    }

public:
    template<SimilarEnumerationT<T> E>
    void extend_values(std::optional<E> from = {}, std::optional<E> to = {}, std::string prefix = {})
    {
        extend<E>(true, from, to, prefix);
    }

    template<SimilarEnumerationT<T> E>
    void extend_names(std::optional<E> from = {}, std::optional<E> to = {}, std::string prefix = {})
    {
        extend<E>(false, from, to, prefix);
    }

    template<typename U, size_t S>
    void extend_all_values(const ConstDynamicEnumeration<U, S> &enumeration, std::string prefix = {})
    {
        extend_all(true, enumeration, prefix);
    }

    template<typename U, size_t S>
    void extend_all_names(const ConstDynamicEnumeration<U, S> &enumeration, std::string prefix = {})
    {
        extend_all(false, enumeration, prefix);
    }

    FrozenEnumeration<T, MAX_SIZE> freeze();
};

// Frozen enumeration, an enumeration that is now frozen and cannot be modified
template<typename UT = dynamic_enum_default_t, size_t MAX_SIZE = std::numeric_limits<UT>::max()>
class FrozenEnumeration : public ConstDynamicEnumeration<UT, MAX_SIZE>
{
protected:
    using builder_type = DynamicEnumeration<UT, MAX_SIZE>;

public:
    FrozenEnumeration() = default;
    explicit FrozenEnumeration(builder_type &&builder) { this->values = std::move(builder.values); }

    FrozenEnumeration &operator=(builder_type &&builder)
    {
        this->values = std::move(builder.values);
        return *this;
    }

    inline static auto create() { return builder_type(); }
};

template<typename T, size_t MAX_SIZE>
FrozenEnumeration<T, MAX_SIZE> DynamicEnumeration<T, MAX_SIZE>::freeze()
{
    return FrozenEnumeration<T, MAX_SIZE>(std::move(*this));
}

template<ConstDynamicEnumerationT T>
class EnumerationDefinition<T>
{
public:
    using value_type = typename T::Value;
    using underlying_type = T::value_type;
    static constexpr bool exists = true;
    static constexpr bool constant = false;
    static constexpr bool singular = false;
};

template<ConstDynamicEnumerationT T>
class Enumeration<T> : public EnumerationDefinition<T>
{
    T const *enumeration = nullptr;
    typename T::Value _value;

public:
    using enumeration_type = T;
    using value_type = typename T::Value;
    using underlying_type = T::value_type;

    Enumeration() = default;
    explicit Enumeration(const enumeration_type &enumeration) : enumeration(&enumeration) {}
    constexpr explicit Enumeration(value_type value) : _value(value) {}
    explicit Enumeration(underlying_type value, const enumeration_type &enumeration)
        : enumeration(&enumeration), _value(enumeration.at(value))
    {
    }
    explicit Enumeration(std::string_view name, const enumeration_type &enumeration)
        : enumeration(&enumeration), _value(enumeration.at(std::string(name)))
    {
    }

    size_t size() const { return enumeration ? enumeration->size() : 0; }

    constexpr value_type value() const { return _value; }
    constexpr underlying_type underlying() const { return static_cast<underlying_type>(_value); }
    constexpr std::string_view name() const { return static_cast<std::string_view>(_value); }

    constexpr operator value_type() const { return value(); }
    constexpr explicit operator const std::string &() const { return static_cast<const std::string &>(_value); }
    constexpr explicit operator std::string_view() const { return name(); }

    bool operator==(const Enumeration &other) const { return _value == other._value; }
    bool operator!=(const Enumeration &other) const { return _value != other._value; }
    bool operator==(const value_type &other) const { return _value == other; }
    bool operator!=(const value_type &other) const { return _value != other; }

    auto &string_to_value_mapping() const { return enumeration->mapping(); }

    auto begin() const { return enumeration->begin(); }
    auto end() const { return enumeration->end(); }

    friend std::ostream &operator<<(std::ostream &os, const Enumeration<enumeration_type> &enumeration)
    {
        return os << static_cast<std::string_view>(enumeration);
    }

    template<typename BasicJsonType>
    static Enumeration<enumeration_type> _from_json(const BasicJsonType &j, const enumeration_type &e)
    {
        if (auto number = _try_from_numeric_json(j); number)
            return Enumeration<enumeration_type>(*number, e);
        return Enumeration<enumeration_type>(j.template get<std::string>(), e);
    }

    template<typename BasicJsonType>
    friend void from_json(const BasicJsonType &j, Enumeration<enumeration_type> &enumeration)
    {
        enumeration = _from_json(j, *enumeration.enumeration);
    }
};

// Static enumeration, a frozen enumeration that is associated with some type
// and thus only has one set of values (static)
template<typename T, typename UT = dynamic_enum_default_t, size_t MAX_SIZE = std::numeric_limits<UT>::max()>
class StaticEnumeration : public FrozenEnumeration<UT, MAX_SIZE>
{
    using Base = FrozenEnumeration<UT, MAX_SIZE>;

public:
    using builder_type = typename Base::builder_type;

    StaticEnumeration() = default;
    explicit StaticEnumeration(builder_type &&builder) : Base(std::move(builder)) {}

    using Base::operator=;

    inline static void finalize(builder_type &&builder);
};

template<typename T, typename UT, size_t MAX_SIZE>
class EnumerationDefinition<StaticEnumeration<T, UT, MAX_SIZE>>
{
public:
    using value_type = typename StaticEnumeration<T, UT, MAX_SIZE>::Value;
    using underlying_type = UT;
    static constexpr bool exists = true;
    static constexpr bool constant = false;
    static constexpr bool singular = true;
};

template<typename T, typename UT, size_t MAX_SIZE>
class Enumeration<StaticEnumeration<T, UT, MAX_SIZE>> : public _Enumeration<StaticEnumeration<T, UT, MAX_SIZE>>,
                                                        public EnumerationDefinition<StaticEnumeration<T, UT, MAX_SIZE>>
{
public:
    using builder_type = DynamicEnumeration<UT, MAX_SIZE>;

private:
    using frozen_type = StaticEnumeration<T, UT, MAX_SIZE>;

    inline static auto &builder()
    {
        static std::unique_ptr<builder_type> _builder;
        if (!_builder)
            _builder = std::make_unique<builder_type>();
        return _builder;
    }

    inline static auto &values()
    {
        static frozen_type _values;
        return _values;
    }

    inline static auto &final_values()
    {
        if (auto &v = values(); !v.empty())
            return v;
        throw std::logic_error("Enumeration not finalized");
    }

    typename frozen_type::Value _value;

public:
    using enumeration_type = frozen_type;
    using value_type = typename frozen_type::Value;
    using underlying_type = UT;

    inline static auto create() { return builder_type(); }
    inline static void finalize()
    {
        if (builder()->empty())
            throw std::logic_error("No enumerations to finalize");

        auto b = *builder();
        builder()->reset();
        finalize(std::move(b));
    }

    inline static const frozen_type &finalize(builder_type &&builder)
    {
        auto &v = values();
        if (v.empty())
            v = std::move(builder);
        else if (v.mapping() != builder.mapping())
            throw std::logic_error("Enumeration redefined with different values");
        return v;
    }

    Enumeration() = default;
    explicit Enumeration(const value_type &value) : _value(value) {}
    explicit Enumeration(underlying_type value) : _value(final_values().at(value)) {}
    explicit Enumeration(std::string_view name) : _value(final_values().at(std::string(name))) {}
    explicit Enumeration(const std::string &name) : _value(final_values().at(name)) {}

    value_type value() const { return _value; }
    underlying_type underlying() const { return static_cast<underlying_type>(_value); }
    std::string_view name() const { return std::string_view(_value); }

    inline static size_t size() { return final_values().size(); }
    inline static constexpr size_t max_size() { return builder_type::max_size(); }

    operator value_type() const { return value(); }
    explicit operator std::string_view() const { return name(); }

    inline static const auto &string_to_value_mapping() { return final_values().mapping(); }

    auto begin() const { return final_values().begin(); }
    auto end() const { return final_values().end(); }

    friend std::ostream &operator<<(std::ostream &os, const Enumeration &enumeration)
    {
        return os << static_cast<std::string_view>(enumeration);
    }
};

template<typename T, typename UT, size_t MAX_SIZE>
void StaticEnumeration<T, UT, MAX_SIZE>::finalize(StaticEnumeration<T, UT, MAX_SIZE>::builder_type &&builder)
{
    Enumeration<StaticEnumeration<T, UT, MAX_SIZE>>::finalize(std::move(builder));
}

template<class E>
struct _EnumerationRelated
{
    using enum_t = E;
    static constexpr bool constant = EnumerationDefinition<E>::constant;
    static constexpr bool exists = EnumerationDefinition<E>::exists;
};
template<class E>
struct _EnumerationRelated<Enumeration<E>>
{
    using enum_t = E;
    static constexpr bool constant = EnumerationDefinition<E>::constant;
    static constexpr bool exists = EnumerationDefinition<E>::exists;
};

template<class E>
inline constexpr static bool has_enum_definition_v = EnumerationDefinition<E>::exists;

template<class E>
inline constexpr static bool is_enumeration_related_v = _EnumerationRelated<E>::exists;

template<class E>
inline constexpr static bool is_constant_enum_v = _EnumerationRelated<E>::constant;

template<class E>
using enum_type_t = typename EnumerationDefinition<E>::value_type;

// Get size of plain enum class (if defined)
template<class E>
inline constexpr size_t enum_size()
{
    static_assert(has_enum_definition_v<E>, "enum is not supported (i.e. not created via make_enum)");
    return Enumeration<E>::size();
}

// Get maximum size (numeric size, assuming starting at 0) of plain enum class (if defined)
// NOTE: Usually used for making an array/vector from a enum with values (i.e. sparse)
template<class E>
inline constexpr size_t enum_max_numeric_size()
{
    static_assert(has_enum_definition_v<E>, "enum is not supported (i.e. not created via make_enum)");
    return Enumeration<E>::max_size();
}

// Convert plain enum value to string (if defined)
template<class E>
inline constexpr std::string_view enum_to_string(E value)
{
    static_assert(has_enum_definition_v<E>, "enum is not supported (i.e. not created via make_enum)");
    return static_cast<std::string_view>(Enumeration<E>(value));
}

// Convert string to plain enum value (if defined)
template<class E>
inline constexpr auto string_to_enum(std::string_view name)
{
    static_assert(has_enum_definition_v<E>, "enum is not supported (i.e. not created via make_enum)");
    return Enumeration<E>(name);
}
// Get a range (i.e. begin/end) for values in plain enum (if defined)
template<class E>
inline constexpr auto each_enum()
{
    static_assert(has_enum_definition_v<E>, "enum is not supported (i.e. not created via make_enum)");
    return Enumeration<E>();
}

namespace internal {

template<typename T>
inline static constexpr size_t max_size(T min, T max)
{
    if constexpr (std::is_unsigned_v<T>) {
        return max - min;
    } else {
        T diff = max - min;
        return diff > 0 ? diff : -diff;
    }
}

} // namespace internal

} // namespace ampere::util

// Specialize various global serialization (json, fmt, ostream)

template<class E>
struct fmt::formatter<ampere::util::Enumeration<E>> : fmt::formatter<string_view>
{
    template<typename FormatContext>
    constexpr auto format(E value, FormatContext &ctx) const
    {
        return fmt::formatter<string_view>::format(::ampere::util::enum_to_string(value), ctx);
    }

    template<typename FormatContext>
    constexpr auto format(ampere::util::Enumeration<E> value, FormatContext &ctx) const
    {
        return fmt::formatter<string_view>::format(std::string_view(value), ctx);
    }
};

template<class E>
struct fmt::formatter<E, std::enable_if_t<ampere::util::has_enum_definition_v<E>, char>>
    : fmt::formatter<std::string_view>
{
    template<typename FormatContext>
    auto format(E value, FormatContext &ctx) const
    {
        return fmt::formatter<std::string_view>::format(::ampere::util::enum_to_string(value), ctx);
    }
};

template<class E>
struct fmt::formatter<std::optional<E>, std::enable_if_t<ampere::util::has_enum_definition_v<E>, char>>
    : formatter<std::string_view>
{
    template<typename FormatContext>
    auto format(std::optional<E> value, FormatContext &ctx) const
    {
        return fmt::formatter<std::string_view>::format(value ? ::ampere::util::enum_to_string(*value) : "none"sv, ctx);
    }
};

NLOHMANN_JSON_NAMESPACE_BEGIN

template<typename E>
struct adl_serializer<E, std::enable_if_t<::ampere::util::has_enum_definition_v<E>, void>>
{
    template<typename BasicJsonType>
    static void from_json(BasicJsonType &&j, E &val)
    {
        val = ::ampere::util::Enumeration<E>::_from_json(j);
    }

    template<typename BasicJsonType>
    static E from_json(BasicJsonType &&j)
    {
        return ::ampere::util::Enumeration<E>::_from_json(j);
    }

    template<typename BasicJsonType>
    static void to_json(BasicJsonType &j, const E &value)
    {
        j = ::ampere::util::enum_to_string<E>(value);
    }
};

NLOHMANN_JSON_NAMESPACE_END

template<class E>
inline auto operator<<(std::ostream &os, const E &value)
    -> std::enable_if_t<ampere::util::has_enum_definition_v<E>, std::ostream &>
{
    return os << ::ampere::util::enum_to_string(value);
}

template<class E>
inline auto operator<<(std::ostream &os, const std::optional<E> &value)
    -> std::enable_if_t<ampere::util::has_enum_definition_v<E>, std::ostream &>
{
    if (value)
        return os << ::ampere::util::enum_to_string(*value);
    return os << "none";
}

#define __make_enum_definition(enum_name, ...)                                                                         \
    template<>                                                                                                         \
    class ampere::util::EnumerationDefinition<enum_name>                                                               \
    {                                                                                                                  \
        template<ampere::util::ConstEnumerationT E>                                                                    \
        friend class ampere::util::ConstEnumIterator;                                                                  \
                                                                                                                       \
    public:                                                                                                            \
        using value_type = enum_name;                                                                                  \
        using underlying_type = std::underlying_type_t<enum_name>;                                                     \
                                                                                                                       \
    protected:                                                                                                         \
        inline static constexpr size_t _size = ::ampere::util::stringified_va_args_size(#__VA_ARGS__);                 \
        inline static constexpr auto strings =                                                                         \
            ::ampere::util::stringified_va_args_to_array<_size, underlying_type>(#__VA_ARGS__);                        \
        inline static constexpr auto _range =                                                                          \
            ::ampere::util::array_pair_value_range<_size, std::string_view, underlying_type>(strings);                 \
        inline static constexpr auto offset = _range.first;                                                            \
        inline static constexpr auto uniformity = ::ampere::util::uniform_range(strings);                              \
        inline static constexpr auto max_size = uniformity == ::ampere::util::RangeUniformity::EXPONENTIAL             \
                                                    ? _size                                                            \
                                                    : ::ampere::util::internal::max_size(_range.first, _range.second); \
        inline static constexpr bool has_values = ::ampere::util::stringified_va_args_has_values(#__VA_ARGS__);        \
        inline static constexpr auto strings_by_value = ::ampere::util::                                               \
            explode_array_pair_values<max_size, _size, std::string_view, underlying_type, offset, uniformity>(         \
                strings);                                                                                              \
                                                                                                                       \
    public:                                                                                                            \
        static constexpr bool exists = true;                                                                           \
        static constexpr bool constant = true;                                                                         \
        static constexpr bool singular = true;                                                                         \
    }

// Make enumeration with string/serialization support
// NOTE: must be used in global namespace
#define make_enum(enum_name, ...) \
    enum class enum_name {        \
        __VA_ARGS__               \
    };                            \
    __make_enum_definition(enum_name, __VA_ARGS__)

// Make enumeration with underlying type (i.e. enum class : <type>)
// NOTE: must be used in global namespace
#define make_enum_with_type(enum_name, underlying_type, ...) \
    enum class enum_name : underlying_type {                 \
        __VA_ARGS__                                          \
    };                                                       \
    __make_enum_definition(enum_name, __VA_ARGS__)

// Make plain enumeration (not enum class) with string/serialization support
// NOTE: must be used in global namespace
#define make_enum_without_class(enum_name, ...) \
    enum enum_name {                            \
        __VA_ARGS__                             \
    };                                          \
    __make_enum_definition(enum_name, __VA_ARGS__)

// Make plain enumeration (not enum class) with an underlying type, including
// string/serialization support
// NOTE: must be used in global namespace
#define make_enum_without_class_with_type(enum_name, underlying_type, ...) \
    enum enum_name : underlying_type {                                     \
        __VA_ARGS__                                                        \
    };                                                                     \
    __make_enum_definition(enum_name, __VA_ARGS__)

// Make enumeration with namespace (final name will be enum_ns::enum_name)
// NOTE: must be used in global namespace
#define make_ns_enum(enum_ns, enum_name, ...) \
    namespace enum_ns {                       \
    enum class enum_name {                    \
        __VA_ARGS__                           \
    };                                        \
    using ::operator<<;                       \
    }                                         \
    __make_enum_definition(::enum_ns::enum_name, __VA_ARGS__)

// Make enumeration with namespace and underlying type (i.e. enum class : <type>)
// NOTE: must be used in global namespace
#define make_ns_enum_with_type(enum_ns, enum_name, underlying_type, ...) \
    namespace enum_ns {                                                  \
    enum class enum_name : underlying_type {                             \
        __VA_ARGS__                                                      \
    };                                                                   \
    using ::operator<<;                                                  \
    }                                                                    \
    __make_enum_definition(::enum_ns::enum_name, __VA_ARGS__)

// Make plain enumeration (not enum class) with namespace (final name will be enum_ns::enum_name)
// NOTE: must be used in global namespace
#define make_ns_enum_without_class(enum_ns, enum_name, ...) \
    namespace enum_ns {                                     \
    enum enum_name {                                        \
        __VA_ARGS__                                         \
    };                                                      \
    using ::operator<<;                                     \
    }                                                       \
    __make_enum_definition(::enum_ns::enum_name, __VA_ARGS__)

// Make plain enumeration (not enum class) with namespace (final name will be enum_ns::enum_name)
// and an underlying type
// NOTE: must be used in global namespace
#define make_ns_enum_without_class_with_type(enum_ns, enum_name, underlying_type, ...) \
    namespace enum_ns {                                                                \
    enum enum_name : underlying_type {                                                 \
        __VA_ARGS__                                                                    \
    };                                                                                 \
    using ::operator<<;                                                                \
    }                                                                                  \
    __make_enum_definition(::enum_ns::enum_name, __VA_ARGS__)

// Define (just the type) a non-constant enumeration
#define define_non_constant_enum(enum_name) \
    struct __##enum_name                    \
    {};                                     \
    using enum_name = ::ampere::util::Enumeration<::ampere::util::StaticEnumeration<__##enum_name>>

// Define (just the type) a non-constant enumeration with some underlying type
#define define_non_constant_enum_with_type(enum_name, underlying_type) \
    struct __##enum_name                                               \
    {};                                                                \
    using enum_name = ::ampere::util::Enumeration<::ampere::util::StaticEnumeration<__##enum_name, underlying_type>>

// Define (just the type) a non-constant enumeration
#define define_non_constant_bounded_enum(enum_name, max_size) \
    struct __##enum_name                                      \
    {};                                                       \
    using enum_name = ::ampere::util::Enumeration<            \
        ::ampere::util::StaticEnumeration<__##enum_name, ::ampere::util::dynamic_enum_default_t, (max_size)>>

// Define (just the type) a non-constant enumeration with some underlying type
#define define_non_constant_bounded_enum_with_type(enum_name, underlying_type, max_size) \
    struct __##enum_name                                                                 \
    {};                                                                                  \
    using enum_name =                                                                    \
        ::ampere::util::Enumeration<::ampere::util::StaticEnumeration<__##enum_name, underlying_type, (max_size)>>

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
