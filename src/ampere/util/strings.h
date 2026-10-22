//
// Copyright (c) 2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#pragma once

#include <cstddef>
#include <iterator>
#include <stdexcept>
#include <type_traits>
#include <string_view>
#include <array>
#include <utility>

using namespace std::literals;

namespace ampere::util {

// Get number of arguments in stringified VA_ARGS
inline static constexpr size_t stringified_va_args_size(std::string_view str)
{
    size_t size = 0;
    for (size_t i = 0; i != std::string_view::npos; i = str.find_first_of(',', i + 1), ++size) {}
    return size;
}

// Check if stringified VA_ARGS has values
inline static constexpr bool stringified_va_args_has_values(std::string_view str)
{
    return str.find_first_of('=') != std::string_view::npos;
}

template<typename T, T BASE = 10>
inline static constexpr T _string_view_to_integer(std::string_view str)
{
    static_assert(std::is_integral_v<T>, "Must be integer type");
    auto value = static_cast<T>(0);
    auto place = static_cast<T>(1);
    for (auto i = str.rbegin(); i != str.rend(); ++i, place *= BASE) {
        if constexpr (!std::is_unsigned_v<T>) {
            if (*i == '-') {
                if (std::next(i) != str.rend())
                    throw std::logic_error("Negative must be at the beginning of number");
                value *= -1;
                continue;
            }
        }
        if (*i >= '0' && *i <= '9') {
            value += static_cast<T>(*i - '0') * place;
        } else {
            if ((*i < 'a' || *i > 'z') && (*i < 'A' || *i > 'Z'))
                throw std::logic_error("Invalid character in value");
            const auto place_value = static_cast<T>(*i - (*i >= 'a' ? 'a' : 'A')) + static_cast<T>(10);
            if (place_value >= BASE)
                throw std::logic_error("Character value outside base-range");
            value += place_value * place;
        }
    }

    return value;
}

// Convert string_view to an integer
template<typename T>
inline static constexpr T string_view_to_integer(std::string_view str)
{
    if (str.size() > 2 && str[0] == '0') {
        if (str[1] == 'x')
            return _string_view_to_integer<T, 16>(str.substr(2));
        if (str[1] == 'b')
            return _string_view_to_integer<T, 2>(str.substr(2));
    }
    return _string_view_to_integer<T>(str);
}

// Split stringified VA_ARGS into argument array
template<size_t N, typename T = unsigned>
inline static constexpr std::array<std::pair<std::string_view, T>, N> stringified_va_args_to_array(std::string_view str)
{
    auto value = static_cast<T>(0);
    std::array<std::pair<std::string_view, T>, N> strings;
    for (size_t i = 0, s = 0; i < N; ++i, ++value) {
        auto e = str.find_first_of(" ,="sv, s);
        // Extract name
        if (e != std::string_view::npos) {
            strings[i].first = str.substr(s, e - s);
            auto c = str.find_first_of(',', e);
            s = str.find_first_not_of(' ', c + 1);

            // Extract value
            if (auto es = str.find_first_of('=', e); es != std::string_view::npos && es < c) {
                auto vs = str.find_first_not_of(' ', es + 1);
                auto ve = str.find_first_of(" ,"sv, vs + 1);
                value =
                    string_view_to_integer<T>(ve == std::string_view::npos ? str.substr(vs) : str.substr(vs, ve - vs));
            }
        } else {
            strings[i].first = str.substr(s);
        }

        strings[i].second = value;
    }
    return strings;
}

} // namespace ampere::util

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
