//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#pragma once

#include <string_view>
#include <sysc/kernel/sc_module.h>
#include <initializer_list>
#include <map>
#include <string>
#include <type_traits>
#include <utility>
#include <list>
#include <vector>

namespace ampere::sc {

template<typename T>
inline constexpr bool is_string_v =
    std::is_same_v<std::string, T> || std::is_same_v<char*, T> || std::is_same_v<const char*, T>;

// Similar to sc_vector but allows for set of names to be provided
// which are used as the name in the hierarchy
template<typename T>
class sc_map : sc_core::sc_module
{
protected:
    std::list<T> elements;
    std::map<std::string, T*> named_elements;
    std::vector<T*> ordered_elements;

    template<typename Iter, typename... Args>
    auto insert(Iter begin, Iter end, Args&&... args)
        -> std::enable_if_t<is_string_v<std::remove_cvref_t<decltype(std::get<std::string>(*std::declval<Iter>()))>>>
    {
        for (auto it = begin; it != end; ++it)
            emplace(std::get<std::string>(*it), std::forward<Args>(args)...);
    }

    template<typename Iter, typename... Args>
    auto insert(Iter begin, Iter end, Args&&... args)
        -> std::enable_if_t<is_string_v<std::remove_cvref_t<decltype(*std::declval<Iter>())>>>
    {
        for (auto it = begin; it != end; ++it)
            emplace(*it, std::forward<Args>(args)...);
    }

    template<typename... Args>
    T& emplace(const char* name, Args&&... args)
    {
        auto& element = elements.emplace_back(name, std::forward<Args>(args)...);
        named_elements.emplace(name, &element);
        ordered_elements.emplace_back(&element);
        return element;
    }

    template<typename... Args>
    T& emplace(const std::string& name, Args&&... args)
    {
        return emplace(name.c_str(), std::forward<Args>(args)...);
    }

public:
    explicit sc_map(const sc_core::sc_module_name& name) : sc_core::sc_module(name) {}

    template<typename Range = std::initializer_list<std::string>, typename... Args>
    explicit sc_map(const sc_core::sc_module_name& name, Range values, Args&&... args) : sc_core::sc_module(name)
    {
        insert(values.begin(), values.end(), std::forward<Args>(args)...);
    }

    template<typename Iter, typename... Args>
    explicit sc_map(const sc_core::sc_module_name& name, Iter begin, Iter end, Args&&... args)
        : sc_core::sc_module(name)
    {
        insert<Iter>(begin, end, std::forward<Args>(args)...);
    }

    T& operator[](unsigned index) { return *ordered_elements[index]; }
    const T& operator[](unsigned index) const { return *ordered_elements[index]; }
    T& operator[](const std::string& name) { return *named_elements.at(name); }
    const T& operator[](const std::string& name) const { return *named_elements.at(name); }

    T& at(unsigned index) { return *ordered_elements.at(index); }
    const T& at(unsigned index) const { return *ordered_elements.at(index); }
    T& at(const std::string& name) { return *named_elements.at(name); }
    const T& at(const std::string& name) const { return *named_elements.at(name); }

    size_t index_of(const std::string& name) const
    {
        auto* e = &at(name);
        size_t index = -1;
        for (const auto* oe : ordered_elements) {
            if (oe == e)
                break;
            ++index;
        }
        return index;
    }

    auto empty() const { return elements.empty(); }
    auto size() const { return elements.size(); }

    auto begin() { return elements.begin(); }
    auto end() { return elements.end(); }
    auto begin() const { return elements.begin(); }
    auto end() const { return elements.end(); }
};

} // namespace ampere::sc

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
