//
// Copyright (c) 2018-2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#pragma once

#include <array>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <string>
#include <vector>

#include <fmt/core.h>
#include <fmt/ranges.h>

#include "ampere/util/bits.h"
#include "ampere/util/enum.h"

namespace ampere::sc {

struct VerifiedValue
{};

template<typename T>
using param_value_reference_type =
    std::conditional_t<std::is_standard_layout_v<T> && std::is_trivial_v<T> && !std::is_pointer_v<T>, T, const T&>;

template<typename T>
class Value : public VerifiedValue
{
protected:
    T value;

public:
    using value_type = T;
    using reference_type = param_value_reference_type<T>;

    Value() = default;
    Value(T value) : value(value) {}

    operator reference_type() const { return value; }

    inline static constexpr bool specialized_specification = false;
    inline static std::string specification() { return ""; }
};

template<typename T>
struct PowerOfTwo : Value<T>
{
    PowerOfTwo() = default;
    PowerOfTwo(T value) : Value<T>(value)
    {
        if (!util::is_power_of_two<T>(value))
            throw std::invalid_argument("not power-of-two");
    }

    inline static std::string specification() { return "power-of-two"; }
};

template<typename T, typename P = void>
struct ValueRange
{
    constexpr static auto min() { return std::numeric_limits<T>::min(); }
    constexpr static auto max() { return std::numeric_limits<T>::max(); }
};

template<typename T>
struct ValueRange<T, std::enable_if_t<util::is_constant_enum_v<T>>>
{
    constexpr static auto min()
    {
        auto e = util::each_enum<T>();
        return *std::min_element(e.begin(), e.end());
    }
    constexpr static auto max()
    {
        auto e = util::each_enum<T>();
        return *std::max_element(e.begin(), e.end());
    }
};

template<typename T, T LOWER = ValueRange<T>::min(), T UPPER = ValueRange<T>::max()>
struct WithinRange : Value<T>
{
    WithinRange() = default;
    WithinRange(T value) : Value<T>(value)
    {
        if (value < LOWER)
            throw std::invalid_argument(fmt::format("less than lower range of {}", LOWER));
        if (value > UPPER)
            throw std::invalid_argument(fmt::format("greater than upper range of {}", UPPER));
    }

    inline static constexpr bool specialized_specification = util::is_constant_enum_v<T>;
    inline static std::string specification()
    {
        if constexpr (util::is_constant_enum_v<T>) {
            std::vector<T> allowed;
            for (auto e : util::each_enum<T>()) {
                if (e >= LOWER && e <= UPPER)
                    allowed.emplace_back(e);
            }
            return fmt::format("{}", fmt::join(allowed, ", "));
        } else {
            if (LOWER != std::numeric_limits<T>::min() && UPPER != std::numeric_limits<T>::max())
                return fmt::format("within range [{}, {}]", LOWER, UPPER);
            if (LOWER != std::numeric_limits<T>::min())
                return fmt::format("within range [{}, max]", LOWER);
            if (UPPER != std::numeric_limits<T>::min())
                return fmt::format("within range [min, {}]", UPPER);
        }
        return "";
    }
};

template<typename T, T LOWER>
using Above = WithinRange<T, LOWER + 1>;

template<typename T, T LOWER>
using AboveOrEqual = WithinRange<T, LOWER>;

template<typename T, T UPPER>
using Below = WithinRange<T, std::numeric_limits<T>::min(), UPPER - 1>;

template<typename T, T UPPER>
using BelowOrEqual = WithinRange<T, std::numeric_limits<T>::min(), UPPER>;

template<typename T>
struct NonZero : Value<T>
{
    NonZero() = default;
    NonZero(T value) : Value<T>(value)
    {
        if (value == static_cast<T>(0))
            throw std::invalid_argument("is zero");
    }
    inline static std::string specification() { return "non-zero"; }
};

template<typename T, T... Values>
struct OneOf : Value<T>
{
    OneOf() = default;
    OneOf(T value) : Value<T>(value)
    {
        if (!((Values == value) || ...))
            throw std::invalid_argument("not one of accepted values");
    }
    inline static std::string specification()
    {
        std::array values = {Values...};
        return fmt::format("one of: {}", fmt::join(values, ", "));
    }
};

} // namespace ampere::sc

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
