//
// Copyright (c) 2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#pragma once

#include <cstddef>
#include <stdexcept>
#include <type_traits>
#include <array>
#include <limits>
#include <utility>

#include "ampere/util/bits.h"

namespace ampere::util {

constexpr size_t MAX_EXPLODED_ARRAY = 512;

// Gather range (min, max) for values in an std::array<std::pair<K, V>>
template<size_t N, typename K, typename V>
inline static constexpr std::pair<V, V> array_pair_value_range(const std::array<std::pair<K, V>, N>& array)
{
    auto min_value = std::numeric_limits<V>::max();
    auto max_value = std::numeric_limits<V>::min();
    for (const auto& [_, value] : array) {
        if (value > max_value)
            max_value = value;
        if (value < min_value)
            min_value = value;
    }

    return {min_value, max_value};
}

template<size_t N, typename K, typename V>
inline static constexpr bool incremental_range_within_limits(const std::array<std::pair<K, V>, N>& array, V limit)
{
    auto [min_value, max_value] = array_pair_value_range(array);
    return (max_value - min_value) < limit;
}

template<size_t N, typename K, typename V>
inline static constexpr bool exponential_range_within_limits(const std::array<std::pair<K, V>, N>& array, V limit)
{
    auto last = array[0].second;
    if (last != 0 && !util::is_power_of_two(last))
        return false;

    V exploded_size = 0;
    for (size_t i = 1; i < N; ++i) {
        if (array[i].second == 0 || !util::is_power_of_two(array[i].second))
            return false;

        exploded_size += util::log2(array[i].second) - (last == 0 ? 0 : util::log2(last));
        if (exploded_size >= limit)
            return false;

        last = array[i].second;
    }

    return true;
}

enum class RangeUniformity {
    NONE,
    INCREMENTAL,
    EXPONENTIAL
};

template<size_t N, typename K, typename V>
inline static constexpr RangeUniformity uniform_range(const std::array<std::pair<K, V>, N>& array)
{
    if (exponential_range_within_limits(array, static_cast<V>(MAX_EXPLODED_ARRAY)))
        return RangeUniformity::EXPONENTIAL;
    if (incremental_range_within_limits(array, static_cast<V>(MAX_EXPLODED_ARRAY)))
        return RangeUniformity::INCREMENTAL;
    return RangeUniformity::NONE;
}

template<size_t M, size_t N, typename V, V OFFSET = 0, RangeUniformity R = RangeUniformity::NONE>
inline static constexpr bool can_explode_array_values_v =
    R == RangeUniformity::EXPONENTIAL || ((M != N || OFFSET != 0) && M < MAX_EXPLODED_ARRAY);

template<RangeUniformity R, typename V, V OFFSET = 0>
inline static constexpr size_t exploded_index(V value)
{
    if constexpr (R == RangeUniformity::EXPONENTIAL) {
        size_t index = value == 0 ? 0 : log2(value);
        return index - (OFFSET == 0 ? 0 : log2(OFFSET));
    } else {
        return static_cast<size_t>(value - OFFSET);
    }
}

// Explode key:value pairs into value:key pairs
template<size_t M,
         size_t N,
         typename K,
         typename V,
         V OFFSET = 0,
         RangeUniformity R = RangeUniformity::NONE,
         bool MUST_BE_UNIQUE = false>
inline static constexpr auto explode_array_pair_values(const std::array<std::pair<K, V>, N>& array)
    -> std::enable_if_t<can_explode_array_values_v<M, N, V, OFFSET, R>, std::array<K, M + 1>>
{
    std::array<K, M + 1> exploded{};
    for (const auto& [name, value] : array) {
        const auto index = exploded_index<R, V, OFFSET>(value);
        if (MUST_BE_UNIQUE && !exploded.at(index).empty())
            throw std::logic_error("Values are not unique");
        exploded[index] = name;
    }

    return exploded;
}

// Explode key:value pairs into value:key pairs (unsupported case)
template<size_t M,
         size_t N,
         typename K,
         typename V,
         V OFFSET = 0,
         RangeUniformity R = RangeUniformity::NONE,
         bool MUST_BE_UNIQUE = false>
inline static constexpr auto explode_array_pair_values(const std::array<std::pair<K, V>, N>& array [[maybe_unused]])
    -> std::enable_if_t<!can_explode_array_values_v<M, N, V, OFFSET, R>, bool>
{
    // Too many values after explosion
    return false;
}

} // namespace ampere::util

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
