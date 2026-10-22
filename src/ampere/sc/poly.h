//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#pragma once

#include <array>
#include <string>
#include <memory>
#include <tuple>
#include <type_traits>
#include <map>
#include <ranges>

#include <boost/core/demangle.hpp>
#include <fmt/format.h>
#include <fmt/ranges.h>
#include <systemc>
#include <typeindex>
#include <utility>

namespace ampere::sc {

class Clock;
class Module;

template<typename... Args>
using make_module_func_args_t = std::tuple<Args&&...>;
template<typename... Args>
using make_module_func_t = Module* (*)(const sc_core::sc_module_name&, make_module_func_args_t<Args...>&&);
using module_implementation_registry_t = std::multimap<std::pair<std::string, std::type_index>, std::string>;

extern bool _register_module(const std::string& type_name, std::type_index constructor, void* make_module);

extern bool _register_module_implementation(const std::string& type_name,
                                            std::type_index constructor,
                                            const std::string& implements_name,
                                            void* make_module);

extern bool _has_registered_module_name(const std::string& type_name);
extern void* _find_registered_module(const std::string& type_name, std::type_index constructor);
extern module_implementation_registry_t& _module_implementation_registry();

template<typename T>
inline std::string class_name()
{
    return boost::core::demangle(typeid(T).name());
}

template<typename T>
inline std::string type_name()
{
    auto name = class_name<T>();
    if constexpr (std::is_const_v<std::remove_reference_t<std::remove_pointer_t<T>>>) {
        name = "const " + name;
    }
    if constexpr (std::is_reference_v<T>) {
        name += "&";
    }
    if constexpr (std::is_pointer_v<T>) {
        name += "*";
    }
    return name;
}

// References are often times added to pointers when using std::forward, remove these unncessary references since
// they're usually not used in constructors
template<typename T>
using normalize_arg_t = std::conditional_t<std::is_pointer_v<std::remove_cvref_t<T>>, std::remove_reference_t<T>, T>;

template<typename T>
inline auto as_const_ref(const T& x) -> const T&
{
    return x;
}

template<typename... Args>
struct poly_constructor
{
    using constructor_t = make_module_func_t<Args...>;
    using args_tuple_t = make_module_func_args_t<Args...>;
    inline static std::type_index type() { return typeid(std::tuple<normalize_arg_t<Args>...>); }

    constructor_t constructor{nullptr};
    explicit poly_constructor(void* func) : constructor(reinterpret_cast<constructor_t>(func)) {}
    explicit poly_constructor(constructor_t func) : constructor(func) {}

    template<typename T>
    static constexpr bool is_constructible()
    {
        return std::is_constructible_v<T, const sc_core::sc_module_name&, Args...>;
    }

    explicit operator bool() const { return constructor; }

    Module* operator()(const sc_core::sc_module_name& name, Args&&... args)
    {
        return constructor(name, std::forward_as_tuple<Args...>(std::forward<Args>(args)...));
    }

    template<typename U>
    U* make(const sc_core::sc_module_name& name, Args&&... args)
    {
        return dynamic_cast<U*>(operator()(name, std::forward<Args>(args)...));
    }
};

template<typename T, typename Constructor = poly_constructor<>>
bool register_module(typename Constructor::constructor_t func)
{
    return _register_module(class_name<T>(), Constructor::type(), reinterpret_cast<void*>(func));
}

template<typename T, typename U, typename Constructor = poly_constructor<>>
bool register_module_implementation(typename Constructor::constructor_t func)
{
    return _register_module_implementation(
        class_name<T>(), Constructor::type(), class_name<U>(), reinterpret_cast<void*>(func));
}

template<typename... Args>
poly_constructor<Args...> find_registered_module(const std::string& type_name)
{
    auto type = poly_constructor<Args...>::type();
    return poly_constructor<Args...>(_find_registered_module(type_name, type));
}

template<class T, class Tuple, std::size_t... I>
constexpr T* _make_module_from_tuple(const sc_core::sc_module_name& name, Tuple&& t, std::index_sequence<I...>)
{
    static_assert(std::is_constructible_v<T, decltype(name), decltype(std::get<I>(std::declval<Tuple>()))...>);
    return new T(name, std::get<I>(std::forward<Tuple>(t))...);
}

template<class T, class Tuple>
constexpr T* make_module_from_tuple(const sc_core::sc_module_name& name, Tuple&& t)
{
    return _make_module_from_tuple<T>(
        name, std::forward<Tuple>(t), std::make_index_sequence<std::tuple_size_v<normalize_arg_t<Tuple>>>{});
}

template<typename T, typename... Constructors>
constexpr bool within_constuctor_list()
{
    return std::is_same_v<T, decltype(std::get<T>(std::declval<std::tuple<Constructors...>>()))>;
}

template<typename T>
struct _poly
{
    template<typename U = T, class Tuple = std::tuple<>>
    static Module* _make_poly(const sc_core::sc_module_name& name, Tuple&& args)
    {
        return make_module_from_tuple<U>(name, std::forward<Tuple>(args));
    }
};

template<typename T, typename... Constructors>
class poly : protected _poly<T>
{
    using self = poly<T, Constructors...>;
    static inline bool _poly = [] {
        if constexpr (sizeof...(Constructors) > 0) {
            return (register_module<T, Constructors>(static_cast<typename Constructors::constructor_t>(
                        &self::template _make_poly<T, typename Constructors::args_tuple_t>))
                    && ...);
        } else {
            return register_module<T>(static_cast<make_module_func_t<>>(&self::template _make_poly<T>));
        }
    }();

protected:
    poly() { this->_poly = _poly; }
};

template<typename T, typename P, typename... Constructors>
class implementation : protected _poly<T>
{
    using self = implementation<T, P, Constructors...>;
    static inline bool _poly = [] {
        if constexpr (sizeof...(Constructors) > 0) {
            return (
                register_module_implementation<T, P, Constructors>(static_cast<typename Constructors::constructor_t>(
                    &self::template _make_poly<T, typename Constructors::args_tuple_t>))
                && ...);
        } else {
            return register_module_implementation<T, P>(
                static_cast<make_module_func_t<>>(&self::template _make_poly<T>));
        }
    }();

protected:
    implementation() { this->_poly = _poly; }
};

template<typename T, typename U, typename... Constructors>
class implements : public U, protected _poly<T>
{
    using self = sc::_poly<T>;
    static inline bool _poly = [] {
        if constexpr (sizeof...(Constructors) > 0) {
            return (
                register_module_implementation<T, U, Constructors>(static_cast<typename Constructors::constructor_t>(
                    &self::template _make_poly<T, typename Constructors::args_tuple_t>))
                && ...);
        } else {
            return register_module_implementation<T, U>(
                static_cast<make_module_func_t<>>(&self::template _make_poly<T>));
        }
    }();

protected:
    template<typename... Args>
    explicit implements(Args&&... args) : U(std::forward<Args>(args)...)
    {
        this->_poly = _poly;
    }
};

template<typename T, typename... Constructors>
struct poly_definition
{
public:
    using interface = T;
    using base = poly<T, Constructors...>;
    template<typename U>
    using implement = implements<U, T, Constructors...>;
    template<typename U>
    using implementation = implementation<U, T, Constructors...>;
};

template<typename T, typename P = T>
constexpr bool is_poly_constructible()
{
    return std::is_base_of_v<_poly<P>, T> && (std::is_same_v<P, T> || std::is_base_of_v<P, T>);
}

template<typename... Args>
void throw_find_module_error(const std::string& type_name)
{
    if (!_has_registered_module_name(type_name))
        throw std::runtime_error{fmt::format("no registered module type {}", type_name)};

    std::array args = {::ampere::sc::type_name<const sc_core::sc_module_name&>(), ::ampere::sc::type_name<Args>()...};
    throw std::runtime_error{fmt::format("module type {} has no constructor ({})", type_name, fmt::join(args, ", "))};
}

template<typename U, typename... Args>
std::unique_ptr<U> make_registered_module(const std::string& type_name,
                                          const std::string& instance_name,
                                          Args&&... args)
{
    auto mod_maker = find_registered_module<Args...>(type_name);
    if (!mod_maker)
        throw_find_module_error<Args...>(type_name);

    auto* p = mod_maker.template make<U>(instance_name.c_str(), std::forward<Args>(args)...);
    if (!p)
        throw std::runtime_error{fmt::format("invalid class for type {}", type_name)};

    return std::unique_ptr<U>(p);
}

template<typename T>
auto find_registered_modules_for_constructor_type(std::type_index type)
{
    auto implements_name = class_name<T>();
    auto [begin, end] = _module_implementation_registry().equal_range({implements_name, type});
    return std::ranges::subrange(begin, end);
}

template<typename T, typename... Args>
auto find_registered_modules_for_constructor()
{
    return find_registered_modules_for_constructor_type<T>(poly_constructor<Args...>::type());
}

template<typename T>
auto find_registered_modules()
{
    auto implements_name = class_name<T>();
    return std::views::all(_module_implementation_registry())
           | std::views::filter([&](const auto& e) { return e.first.first == implements_name; });
}

} // namespace ampere::sc

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
