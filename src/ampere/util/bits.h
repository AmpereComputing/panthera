//
// Copyright (c) 2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include <cassert>
#include <climits>
#include <cstdint>
#include <stdexcept>
#include <fmt/core.h>

namespace ampere::util {

template<typename T = std::uint64_t, typename R = std::size_t>
constexpr R bitsize(T val)
{
    return val == static_cast<T>(0) ? 0 : (1 + bitsize(val >> 1));
}

template<typename T = std::uint64_t, typename R = std::size_t>
constexpr R log2(T val)
{
    if (val > 0)
        return bitsize(val) - 1;
    throw std::runtime_error("val must be > 0");
}

template<typename T = std::uint64_t>
constexpr T mask(int nbits)
{
    constexpr int max_bits = sizeof(T) * CHAR_BIT;
    if (nbits > max_bits || nbits < 0)
        throw std::invalid_argument(fmt::format("nbits must be in [0,{}]", max_bits));
    return nbits == max_bits ? ~T{0} : (T{1} << (nbits)) - 1;
}

template<typename T = std::uint64_t>
constexpr T fold_to_n_bits(T val, int nbits)
{
    constexpr int max_bits = sizeof(T) * CHAR_BIT;
    if (nbits >= max_bits)
        return val;
    if (nbits == 0)
        return 0;

    T folded_val = val;

    while (val) {
        val >>= nbits;
        folded_val ^= val;
    }

    return folded_val & mask(nbits);
}

// Create mask starting at bit `lo` and extending to bit `hi`. For example,
// `mask(2, 1)` will return 0b110.
template<typename T = std::uint64_t>
constexpr T mask(int hi, int lo)
{
    constexpr int max_bits = sizeof(T) * CHAR_BIT;
    if (hi < 0 || lo < 0 || hi >= max_bits || lo >= max_bits || lo > hi)
        throw std::invalid_argument(fmt::format("hi and lo must be in [0,{}] where hi >= lo", max_bits - 1));
    int w = hi - lo + 1;
    return mask<T>(w) << lo;
}

// Extracts bits `lo` to `hi` (inclusive) from `val`.
template<typename T = std::uint64_t>
constexpr std::uint64_t extract(T val, int hi, int lo)
{
    return (val & mask<T>(hi, lo)) >> lo;
}

template<typename T = std::uint64_t>
constexpr bool is_power_of_two(T val)
{
    if (val == static_cast<T>(0))
        throw std::invalid_argument("val must be > 0");
    return (val & (val - 1)) == static_cast<T>(0);
}

template<typename T = uint64_t>
constexpr int factor2(T n)
{
    // power of 2 in the prime factorization of n
    int c = 0;
    while (n && !(n & 1)) {
        c++;
        n >>= 1;
    }
    return c;
}

template<class T, class U>
constexpr T narrow_cast(U&& u) noexcept
{
    return static_cast<T>(std::forward<U>(u));
}

} // namespace ampere::util
