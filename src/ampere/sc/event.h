//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include <boost/smart_ptr/local_shared_ptr.hpp>
#include <cstddef>
#include <cstdint>
#include <ostream>
#include <stdexcept>
#include <string>
#include <optional>
#include <systemc>
#include <tuple>
#include <type_traits>

#include "ampere/event_stream/event_base.h"
#include "ampere/event_stream/event_stream.h"
#include "ampere/util/enum.h"

namespace ampere::sc {

template<typename T>
struct EventArgBase
{
    using value_type = T;
    using data_value_type = T;
    event_stream::DataType data_type;
    T data_value;
};

template<typename T>
struct EventArg : EventArgBase<T>
{
    explicit operator bool() const { return true; }
    const T& value() const { return this->data_value; }
};

template<typename T>
struct EventArg<std::optional<T>> : EventArgBase<std::optional<T>>
{
    using data_value_type = T;
    explicit operator bool() const { return static_cast<bool>(this->data_value); }
    const T& value() const { return *this->data_value; }
};

template<typename T>
EventArg<T> to_event_arg(EventArg<T> x)
{
    return x;
}

template<typename T>
struct EventArgs
{
    static auto make(T x) { return to_event_arg(x); }
};

template<typename T>
struct EventArgs<EventArg<T>>
{
    static auto make(EventArg<T> x) { return x; }
};

template<typename X, typename Y>
struct EventArgs<std::pair<X, Y>>
{
    static auto make(std::pair<X, Y> arg) { return std::make_tuple(to_event_arg(arg.first), to_event_arg(arg.second)); }
};

template<typename... Ts>
struct EventArgs<std::tuple<Ts...>>
{
    static auto make(std::tuple<Ts...> arg)
    {
        return std::apply([](const auto&... args) { return std::make_tuple(to_event_arg(args)...); }, arg);
    }
};

template<typename T>
auto to_event_args(T&& x)
{
    return EventArgs<std::remove_cvref_t<T>>::make(std::forward<T>(x));
}

template<typename T>
std::pair<bool, event_stream::EnumMappingType> event_data_enumerations()
{
    static_assert(util::has_enum_definition_v<T>, "Enumeration does not have a definition");
    bool complete = true;
    event_stream::EnumMappingType mapping;
    if constexpr (util::is_constant_enum_v<T>) {
        const auto& name_value_mapping = util::Enumeration<T>::string_to_value_mapping();
        for (const auto& [enum_name, enum_value] : name_value_mapping)
            mapping.emplace(static_cast<event_stream::EnumMappingType::key_type>(enum_value), enum_name);
    } else {
        complete = false;
    }

    return {complete, mapping};
}

namespace internal {

class EventDataBase : public sc_core::sc_object
{
public:
    explicit EventDataBase(const std::string& name) : sc_core::sc_object(name.c_str()), definition(-1) {}
    EventDataBase(const std::string& name, const std::string& description)
        : sc_core::sc_object(name.c_str()), definition(make_type(this->name(), description))
    {
    }

    template<typename T>
    EventDataBase(const std::string& name, const std::string& description)
        : sc_core::sc_object(name.c_str()), definition(make_enum_type<T>(this->name(), description))
    {
    }

    event_stream::DataType get_data_type() const { return definition; }

protected:
    event_stream::EventStream* get_event_stream();

    event_stream::DataType make_type(const std::string& name, const std::string& description)
    {
        if (auto* es = get_event_stream(); es)
            return es->define_data(name, description);
        return event_stream::EventStream::define_global_data(name, description);
    }

    event_stream::DataType make_anonymous_enum_type(const std::string& name,
                                                    const std::string& description,
                                                    const event_stream::EnumMappingType& enumerations)
    {
        if (auto* es = get_event_stream(); es)
            return es->define_data_with_anonymous_enum(name, description, enumerations);
        return event_stream::EventStream::define_global_data_with_anonymous_enum(name, description, enumerations);
    }

    template<typename T>
    event_stream::DataType make_enum_type(const std::string& name,
                                          const std::string& description,
                                          bool complete,
                                          const event_stream::EnumMappingType& enumerations)
    {
        if (auto* es = get_event_stream(); es) {
            if (complete)
                return es->define_data_with_enum<T>(name, description, enumerations);
            return es->define_data_with_enum<T>(name, description);
        }

        if (complete)
            return event_stream::EventStream::define_global_data_with_enum<T>(name, description, enumerations);
        return event_stream::EventStream::define_global_data_with_enum<T>(name, description);
    }

    template<typename T, typename ET = T>
    event_stream::DataType make_enum_type(const std::string& name, const std::string& description)
    {
        auto [complete, enumerations] = event_data_enumerations<ET>();
        if (complete && enumerations.empty())
            return make_type(name, description);
        return make_enum_type<T>(name, description, complete, enumerations);
    }

    template<typename T>
    event_stream::DataType finalize_enum_type(const event_stream::EnumMappingType& enumerations)
    {
        if (auto* es = this->get_event_stream(); es)
            return es->provide_enum<T>(enumerations);
        return event_stream::EventStream::provide_global_enum<T>(enumerations);
    }

    template<typename T>
    inline static event_stream::EnumMappingType enumeration_as_mapping(const T& enumeration)
    {
        event_stream::EnumMappingType mapping;
        for (const auto& [value, str] : enumeration)
            mapping.emplace(static_cast<int64_t>(value), str);
        return mapping;
    }

    event_stream::DataType definition;
};

template<typename T>
class EventDataCommon : public EventDataBase
{
public:
    explicit EventDataCommon(const std::string& name) : EventDataBase(name) {}
    EventDataCommon(const std::string& name, const std::string& description) : EventDataBase(name, description) {}

    EventArg<T> operator()(T val) { return {definition, val}; }
    EventArg<std::optional<T>> optional(std::optional<T> val) { return {definition, val}; }

    EventArg<std::optional<T>> if_set(T val) { return {definition, val ? std::optional<T>(val) : std::nullopt}; }
    EventArg<std::optional<T>> if_set(T val, bool pred)
    {
        return {definition, pred ? std::optional<T>(val) : std::nullopt};
    }
};

template<typename UT, typename VT>
class EventDataDynamicEnumCommon : public EventDataCommon<UT>
{
    using Base = EventDataCommon<UT>;

public:
    explicit EventDataDynamicEnumCommon(const std::string& name) : Base(name) {}

    using Base::operator();
    EventArg<UT> operator()(const VT& val) { return {this->definition, static_cast<UT>(val)}; }

    using Base::optional;
    EventArg<std::optional<UT>> optional(std::optional<VT> val)
    {
        return {this->definition, val ? static_cast<UT>(*val) : std::nullopt};
    }

    using Base::if_set;
    EventArg<std::optional<UT>> if_set(const VT& val)
    {
        return {this->definition, val ? std::optional<UT>(val) : std::nullopt};
    }
    EventArg<std::optional<UT>> if_set(const VT& val, bool pred)
    {
        return {this->definition, pred ? std::optional<UT>(val) : std::nullopt};
    }
};

} // namespace internal

template<typename T, typename P = void>
class EventData : public internal::EventDataCommon<T>
{
public:
    EventData(const std::string& name, const std::string& description) : internal::EventDataCommon<T>(name, description)
    {
    }
};

template<typename T>
class EventData<T, std::enable_if_t<std::is_enum_v<T>, void>> : public internal::EventDataCommon<T>
{
public:
    EventData(const std::string& name, const std::string& description) : internal::EventDataCommon<T>(name)
    {
        this->definition = this->template make_enum_type<T>(this->name(), description);
    }
};

template<typename UT, size_t MAX_SIZE>
class EventData<const util::FrozenEnumeration<UT, MAX_SIZE>, void>
    : public internal::EventDataDynamicEnumCommon<UT, typename util::FrozenEnumeration<UT, MAX_SIZE>::Value>
{
    using T = util::FrozenEnumeration<UT, MAX_SIZE>;
    using VT = typename T::Value;
    using Base = internal::EventDataDynamicEnumCommon<UT, typename util::FrozenEnumeration<UT, MAX_SIZE>::Value>;

public:
    EventData(const std::string& name, const std::string& description, const T& enumeration) : Base(name)
    {
        auto mapping = this->enumeration_as_mapping(enumeration);
        this->definition = this->make_anonymous_enum_type(this->name(), description, mapping);
    }
};

template<typename UT, size_t MAX_SIZE>
class EventData<util::FrozenEnumeration<UT, MAX_SIZE>, void>
    : public EventData<const util::FrozenEnumeration<UT, MAX_SIZE>, void>
{
public:
    using EventData<const util::FrozenEnumeration<UT, MAX_SIZE>, void>::EventData;
};

template<typename ET, typename UT, size_t MAX_SIZE>
class EventData<util::Enumeration<util::StaticEnumeration<ET, UT, MAX_SIZE>>, void>
    : public internal::EventDataDynamicEnumCommon<UT, typename util::StaticEnumeration<ET, UT, MAX_SIZE>::Value>
{
    using ST = util::StaticEnumeration<ET, UT, MAX_SIZE>;
    using T = util::Enumeration<ST>;
    using VT = typename T::value_type;
    using Base = internal::EventDataDynamicEnumCommon<UT, typename util::StaticEnumeration<ET, UT, MAX_SIZE>::Value>;

public:
    EventData(const std::string& name, const std::string& description) : Base(name)
    {
        this->definition = this->template make_enum_type<T, ST>(this->name(), description);
    }

    using Base::operator();
    EventArg<UT> operator()(const std::string& val) { return {this->definition, T(val).underlying()}; }

    using Base::optional;
    EventArg<std::optional<UT>> optional(std::optional<std::string> val)
    {
        return {this->definition, val ? T(*val).underlying() : std::nullopt};
    }

    using Base::if_set;
    EventArg<std::optional<UT>> if_set(const std::string& val)
    {
        auto underlying_value = T(val).underlying();
        return {this->definition, underlying_value ? underlying_value : std::nullopt};
    }
    EventArg<std::optional<UT>> if_set(const std::string& val, bool pred)
    {
        return {this->definition, pred ? T(val).underlying() : std::nullopt};
    }

    const auto& finalize(typename T::builder_type&& builder)
    {
        const auto& frozen = T::finalize(std::move(builder));
        auto mapping = this->enumeration_as_mapping(frozen);
        this->template finalize_enum_type<T>(mapping);
        return frozen;
    }
};

class Event : public sc_core::sc_object, public event_stream::EventBase
{
public:
    Event(const std::string& name, const std::string& description);
    Event(event_stream::EventStream& event_stream, const std::string& name, const std::string& description);

    const char* kind() const override { return "ampere::sc::Event"; }

    event_stream::EventType get_event_type() const { return definition; }

    void operator()();
    void at(sc_core::sc_time at_time);

    template<typename T, typename... Ts>
    void operator()(T&& first, Ts&&... rest);

    template<typename T, typename... Ts>
    void at(sc_core::sc_time at_time, T&& first, Ts&&... rest);

    bool is_enabled() const { return enabled; }

    void enable() override { enabled = true; }
    void disable() override { enabled = false; }

    static void enable_all();
    static void disable_all();

protected:
    event_stream::EventStream& event_stream;
    event_stream::EventType definition;
    bool enabled;
};

namespace {

template<typename T>
using is_signed = typename std::enable_if_t<std::is_signed_v<T>>;
template<typename T>
using is_signed_enum = typename std::enable_if_t<std::is_enum_v<T> && std::is_signed_v<std::underlying_type_t<T>>>;

template<typename T>
using is_unsigned = typename std::enable_if_t<std::is_unsigned_v<T>>;
template<typename T>
using is_unsigned_enum = typename std::enable_if_t<std::is_enum_v<T> && std::is_unsigned_v<std::underlying_type_t<T>>>;

template<typename T>
inline is_signed<T> add_datum(event_stream::EventStream& stream,
                              event_stream::EventHandle eh,
                              event_stream::DataType data_type,
                              T data_value)
{
    return stream.add_int_data(eh, data_type, data_value);
}

template<typename T>
inline is_signed_enum<T> add_datum(event_stream::EventStream& stream,
                                   event_stream::EventHandle eh,
                                   event_stream::DataType data_type,
                                   T data_value)
{
    return stream.add_int_data(eh, data_type, static_cast<std::underlying_type_t<T>>(data_value));
}

template<typename T>
inline is_unsigned<T> add_datum(event_stream::EventStream& stream,
                                event_stream::EventHandle eh,
                                event_stream::DataType data_type,
                                T data_value)
{
    return stream.add_uint_data(eh, data_type, data_value);
}

template<typename T>
inline is_unsigned_enum<T> add_datum(event_stream::EventStream& stream,
                                     event_stream::EventHandle eh,
                                     event_stream::DataType data_type,
                                     T data_value)
{
    return stream.add_uint_data(eh, data_type, static_cast<std::underlying_type_t<T>>(data_value));
}

inline void add_datum(event_stream::EventStream& stream,
                      event_stream::EventHandle eh,
                      event_stream::DataType data_type,
                      const char* data_value)
{
    stream.add_string_data(eh, data_type, data_value);
}

inline void add_datum(event_stream::EventStream& stream,
                      event_stream::EventHandle eh,
                      event_stream::DataType data_type,
                      const std::string& data_value)
{
    stream.add_string_data(eh, data_type, data_value);
}

template<typename T>
inline void add_datum(event_stream::EventStream& stream,
                      event_stream::EventHandle eh,
                      event_stream::DataType data_type,
                      boost::local_shared_ptr<T> data_value)
{
    auto arg = to_event_arg(data_value->txid());
    stream.add_uint_data(eh, arg.data_type, arg.data_value);
    stream.add_uint_data(eh, data_type, data_value.local_use_count() - 2); // don't count usage inside the event library
}

template<typename T>
struct arg_as_tuple
{
    static std::tuple<T> make(T&& arg) { return std::forward_as_tuple(arg); }
};

template<typename... Ts>
struct arg_as_tuple<std::tuple<Ts...>>
{
    static std::tuple<Ts...> make(std::tuple<Ts...> arg) { return arg; }
};

template<typename... Ts>
using ArgPack = std::tuple<EventArg<Ts>...>;

template<typename... Ts>
auto make_arg_pack(Ts&&... args)
{
    return std::tuple_cat(
        arg_as_tuple<decltype(to_event_args(std::forward<Ts>(args)))>::make(to_event_args(std::forward<Ts>(args)))...);
}

template<int N>
struct arg_iter
{
    template<typename... Ts>
    static void add_data(event_stream::EventStream& stream, event_stream::EventHandle eh, const ArgPack<Ts...>& args)
    {
        arg_iter<N - 1>::add_data(stream, eh, args);
        const auto& arg = std::get<N - 1>(args);
        if (static_cast<bool>(arg))
            add_datum(stream, eh, arg.data_type, arg.value());
    }
};

template<>
struct arg_iter<0>
{
    template<typename... Ts>
    static void add_data(event_stream::EventStream& stream [[maybe_unused]],
                         event_stream::EventHandle eh [[maybe_unused]],
                         const ArgPack<Ts...>& args [[maybe_unused]])
    {
    }
};

} // namespace

inline void Event::operator()()
{
    if (enabled && event_stream.is_enabled())
        event_stream.post_event(definition, sc_core::sc_time_stamp().value());
}

inline void Event::at(sc_core::sc_time at_time)
{
    if (enabled && event_stream.is_enabled())
        event_stream.post_event(definition, at_time.value());
}

template<typename T, typename... Ts>
void Event::operator()(T&& first, Ts&&... rest)
{
    if (enabled && event_stream.is_enabled()) {
        auto args = make_arg_pack(first, std::forward<Ts>(rest)...);
        auto eh = event_stream.open_event(definition, sc_core::sc_time_stamp().value());
        arg_iter<std::tuple_size<decltype(args)>::value>::add_data(event_stream, eh, args);
        event_stream.close_event(eh);
    }
}

template<typename T, typename... Ts>
void Event::at(sc_core::sc_time at_time, T&& first, Ts&&... rest)
{
    if (enabled && event_stream.is_enabled()) {
        auto args = make_arg_pack(first, std::forward<Ts>(rest)...);
        auto eh = event_stream.open_event(definition, at_time.value());
        arg_iter<std::tuple_size<decltype(args)>::value>::add_data(event_stream, eh, args);
        event_stream.close_event(eh);
    }
}

#define AMPERE_EVENT_DEFN_NAME(NAME) _ev_##NAME##_defn
#define AMPERE_EVENT_DEFN(NAME, DESCRIPTION)                         \
    ::ampere::event_stream::EventType AMPERE_EVENT_DEFN_NAME(NAME) = \
        ::ampere::event_stream::EventStream::define_global_event(#NAME, DESCRIPTION);

#define AMPERE_EVENT_DATA_DEFN_NAME(NAME) _evd_##NAME##_defn
#define AMPERE_EVENT_DATA_DEFN(NAME, DESCRIPTION)                        \
    ::ampere::event_stream::DataType AMPERE_EVENT_DATA_DEFN_NAME(NAME) = \
        ::ampere::event_stream::EventStream::define_global_data(#NAME, DESCRIPTION);

#define AMPERE_EVENT_DATA_ENUMERATIONS_DEFN(ENUM, NAME, DESCRIPTION)             \
    ::ampere::event_stream::DataType AMPERE_EVENT_DATA_DEFN_NAME(NAME) =         \
        ::ampere::event_stream::EventStream::define_global_data_with_enum<ENUM>( \
            #NAME, DESCRIPTION, ::ampere::sc::event_data_enumerations<ENUM>().second);

#define AMPERE_EVENT_DATA_FUNC(DECL, NAME, EXPR)                               \
    extern ::ampere::event_stream::DataType AMPERE_EVENT_DATA_DEFN_NAME(NAME); \
    inline auto to_event_arg(DECL) -> ampere::sc::EventArg<decltype(EXPR)>     \
    {                                                                          \
        return {AMPERE_EVENT_DATA_DEFN_NAME(NAME), (EXPR)};                    \
    }

#define AMPERE_EVENT_ENUM(ENUM, NAME)                                          \
    extern ::ampere::event_stream::DataType AMPERE_EVENT_DATA_DEFN_NAME(NAME); \
    inline auto to_event_arg(ENUM e) -> ampere::sc::EventArg<ENUM>             \
    {                                                                          \
        return {AMPERE_EVENT_DATA_DEFN_NAME(NAME), e};                         \
    }

} // namespace ampere::sc

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
