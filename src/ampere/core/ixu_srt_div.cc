//
// Copyright (c) 2024 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

// "SRT division, taking its name from the initials of Sweeney,
// Robertson [1] and Tocher [2], who developed the algorithm
// independently at approximately the same time."  - Reference: "SRT
// division architectures and implementations", Harris, Oberman and
// Horowitz, https://pages.hmc.edu/harris/research/SRT.pdf

// See also the "Furiosa DIV_SQRT MAS v2" in our internal
// documentation.

// The goal of this file is to compute the number of iterations that a
// FLOATING POINT radix-16 divider would require before the remainder
// reaches zero. We don't care about the quotient, so there is nothing
// here that maintains the floating point exponent.

// The radix-16 divider is emulated by two passes of radix-4 division.
// This is actually how the Connor CPU hardware works, doing two
// radix-4 steps per cycle.  Each pass of radix-4 division generates
// two quotient bits. Each pass of radix-16 division generates 4
// quotient bits. So we divide the number of radix-4 steps by two,
// rounding up, to get the number of radix-16 iterations.

// For connor we are only doing variable latency division for fp32 and
// fp64. This file supports fp16 as well.

#if defined(__aarch64__)
#include <arm_fp16.h>
#endif
#include <cstdint>
#include <cassert>
#include <cstdlib>
#include <string>
#include <iostream>
#include <sstream>
#include <fmt/core.h>
#include <boost/multiprecision/cpp_int.hpp>
namespace bm = boost::multiprecision;
#include "ixu_srt_div.h"

// #define DEBUG_SRT_DIV
#if defined(DEBUG_SRT_DIV)
#define DBG_SRT(format_str, ...) fmt::print(stderr, format_str "\n", ##__VA_ARGS__)
#else
#define DBG_SRT(format_str, ...) \
    do {                         \
    } while (0)
#endif

namespace ampere::core::srt_div {

template<class T>
std::string hstr(const T& v)
{ // emit as hex, convert to string for fmt
    std::ostringstream os;
    os << std::hex << v;
    return os.str();
}

// x (2nd) index is 1.00, 1,01, 1.10, 1.11
// upper 3b of normalized quotient
// y (1st) index is upper 5b of partial remainder
// using 9 as an out-of-bounds value, used for checking
//
// This table is copied from the Furiosa documentation mentioned
// above.
static std::int8_t multiplier_table[32][4] = {
    // positive values
    {0, 0, 0, 0}, // 0000.0
    {1, 1, 1, 1}, // 0000.1
    {1, 1, 1, 1}, // 0001.0
    {2, 2, 1, 1}, // 0001.1
    {2, 2, 1, 1}, // 0010.0
    {3, 2, 2, 2}, // 0010.1
    {3, 3, 2, 2}, // 0011.0
    {3, 3, 2, 2}, // 0011.1
    {3, 3, 3, 3}, // 0100.0
    {3, 3, 3, 3}, // 0100.1
    {9, 3, 3, 3}, // 0101.0
    {9, 3, 3, 3}, // 0101.1
    {9, 9, 3, 3}, // 0110.0
    {9, 9, 3, 3}, // 0110.1
    {9, 9, 9, 3}, // 0111.0
    {9, 9, 9, 3}, // 0111.1
    // negative values
    {9, 9, 9, -3},    // 1000.0
    {9, 9, -3, -3},   // 1000.1
    {9, 9, -3, -3},   // 1001.0
    {9, -3, -3, -3},  // 1001.1
    {9, -3, -3, -3},  // 1010.0
    {-3, -3, -3, -3}, // 1010.1
    {-3, -3, -3, -3}, // 1011.0
    {-3, -3, -2, -2}, // 1011.1
    {-3, -3, -2, -2}, // 1100.0
    {-3, -2, -2, -2}, // 1100.1
    {-2, -2, -2, -2}, // 1101.0
    {-2, -2, -1, -1}, // 1101.1
    {-1, -1, -1, -1}, // 1110.0
    {-1, -1, -1, -1}, // 1110.1
    {-1, -1, -1, -1}, // 1111.0
    {0, 0, 0, 0},     // 1111.1
};

[[maybe_unused]] static std::string emit_binary(bm::uint128_t input_value,
                                                std::uint64_t integer_bits = 64,
                                                std::uint64_t length = 128)
{
    std::ostringstream os;
    std::uint64_t j = 0;
    for (std::uint64_t i = 0; i < length; ++i) {
        if (i == integer_bits) {
            os << ".";
            j = 0;
        } else if (j == 4) {
            os << "'";
            j = 0;
        }
        ++j;
        std::uint8_t bit = static_cast<std::uint8_t>(input_value >> (length - i - 1)) & 1;
        os << (bit ? '1' : '0');
    }
    return os.str();
}

static std::int8_t lookup_dmul(std::uint8_t upper_partial_remainder, std::uint8_t upper_divisor)
{
    DBG_SRT("lookup_dmul args: upper partial remainder: {:02x}     upper divisor fraction: {:1x}",
            upper_partial_remainder,
            upper_divisor);
    DBG_SRT("{} upper_partial_remainder", emit_binary(upper_partial_remainder, 4, 5));
    assert(upper_divisor < 4);
    assert(upper_partial_remainder < 32);
    std::int8_t dmul = multiplier_table[upper_partial_remainder][upper_divisor];
    assert(dmul != 9);
    return dmul;
}

static std::uint8_t select_prem(const bm::uint128_t& partial_remainder)
{
    std::uint8_t rv =
        static_cast<std::uint8_t>(partial_remainder >> (64 - 1)); // 4 integer bits and one fractional bit (xxxx.y)
    return rv & 0x1F;
}

static std::uint8_t select_divisor(const bm::uint128_t& divisor)
{
    // looking for 0001.00, 0001.01, 0001.10, 0001.11
    const bm::uint128_t top = (divisor >> 62) & 0x3F; // top 6 bits  but upper 3b must be 0
    assert((top & 0b111000) == 0);                    // make sure divisor is aligned properly
    assert((top & 0b000100) != 0);                    // make sure divisor has a 1 in bit 60
    return static_cast<std::uint8_t>(top) & 0x3;      // mask off the integral bit, return the factional part
}

class quotient_t
{
public:
    std::uint64_t qpos{0};
    std::uint64_t qneg{0};

    void add_bits(std::int64_t mul)
    {
        qpos <<= 2;
        qneg <<= 2;
        if (mul > 0) {
            assert(mul < 4);
            qpos |= mul;
        } else {
            auto nmul = std::abs(mul);
            assert(nmul < 4);
            qneg |= nmul;
        }
    }

    std::int64_t quotient() const { return static_cast<std::int64_t>(qpos + ~qneg + 1); }
};

static bm::uint128_t step(quotient_t& q, const bm::uint128_t& partial_remainder, const bm::uint128_t& divisor)
{
    const std::uint64_t shift = 2; // radix=4
    const bm::uint128_t p = partial_remainder << shift;
    const std::int8_t dmul = lookup_dmul(select_prem(p), select_divisor(divisor));
    DBG_SRT("dmul = {}", dmul);
    q.add_bits(dmul);
    bm::uint128_t next_partial_remainder = p - divisor * static_cast<std::int64_t>(dmul);
    return next_partial_remainder;
}

struct aligned_int_t
{
    bm::uint128_t x;
    unsigned right_shifts{0};

    aligned_int_t(const bm::uint128_t& v) : x(v) {}
};

static unsigned srt_divide_radix_4_iterations(const aligned_int_t& num,
                                              const aligned_int_t& den,
                                              const unsigned max_iterations)
{
    // radix 4 generates 2 quotient bits per cycle.
    // radix 16 generates 4 quotient bits per cycle.
    // we emulate radix 16 by doing two iterations of radix 4.

    unsigned iter = 0;
    bm::uint128_t partial_remainder = num.x;
    quotient_t q;

    // Ensure first partial remainder is less than the denominator.
    // Integer args are were already fixed in the setup

    while (partial_remainder >= den.x) {
        partial_remainder = partial_remainder >> 1;
    }
    DBG_SRT("{} scaled partial remainder", emit_binary(partial_remainder));
    for (; iter < max_iterations; iter++) {
        auto next_partial_remainder = step(q, partial_remainder, den.x);
#if defined(DEBUG_SRT_DIV)
        fmt::print("{} next_partial_remainder. iteration={}\n", hstr(next_partial_remainder), iter);
        fmt::print("next_partial_remainder: {}\n", emit_binary(next_partial_remainder));
        fmt::print("Quotient: qpos {:016x} {}\n", q.qpos, emit_binary(q.qpos, 0, 64));
        fmt::print("Quotient: qneg {:016x} {}\n", q.qneg, emit_binary(q.qneg, 0, 64));

        auto quotient = q.quotient();
        fmt::print("Sum qpos+qneg {} {:016x}\n", quotient, quotient);

        bm::uint128_t fractional_quotient = quotient;
        // move the bits to the max fractional quotient position
        fractional_quotient = fractional_quotient << (64 - (2 * (iter + 1)));
        fmt::print("Fractional quotient {} {}\n", hstr(fractional_quotient), emit_binary(fractional_quotient, 64, 128));
        // postscale, undoing the shifts that got us started
        const unsigned net_shifts = num.right_shifts - den.right_shifts;
        bm::uint128_t adjusted_quotient = fractional_quotient << net_shifts;
        fmt::print("Quotient:    q {} {}\n", hstr(adjusted_quotient), emit_binary(adjusted_quotient, 64, 128));
#endif
        if (next_partial_remainder == 0) {
            DBG_SRT("Required radix-4 iterations: {}", iter + 1);
            return iter + 1; // zero based counting
        }
        partial_remainder = next_partial_remainder;
    }
    DBG_SRT("WARNING: HIT {} iterations without zeroing remainder!", max_iterations);
    return max_iterations;
}

union fp64_t
{
    struct
    {
        std::uint64_t fraction : 52;
        std::uint64_t exponent : 11;
        std::uint64_t sign : 1;
    } s;
    std::uint64_t u;
    double f;
    bm::uint128_t to_fixed_point() const
    { // 4 integer bits, 60 fraction bits
        bm::uint128_t t = 0;
        if (s.exponent > 0) {
            // bit 52 is the implicit fraction bit made explicit
            t = (1ULL << 52ULL) | s.fraction;
            t = t << (64 - 52); // move bit 52 to bit 64
        } else {                // denorm
            std::uint64_t tt = s.fraction;
            if (tt) {
                int leading_zeros = __builtin_clzll(tt);
                t = tt;
                t = t << (leading_zeros + 3); // fixed point with leading 1-value digit in bit 64
            }
        }
        return t;
    }
};
union fp32_t
{
    struct
    {
        std::uint32_t fraction : 23;
        std::uint32_t exponent : 8;
        std::uint32_t sign : 1;
    } s;
    std::uint32_t u;
    float f;

    bm::uint128_t to_fixed_point() const
    {
        bm::uint128_t t = 0;
        if (s.exponent > 0) {
            // bit 23 is the implicit fraction bit made explicit
            t = (1ULL << 23ULL) | s.fraction;
            t = t << (64 - 23); // move bit 23 to bit 64
        } else {                // denorm
            std::uint64_t tt = s.fraction;
            if (tt > 0) {
                int leading_zeros = __builtin_clzll(tt);
                t = tt;
                t = t << (leading_zeros + 1);
            }
        }
        return t;
    }
};

union fp16_t
{
    struct
    {
        std::uint16_t fraction : 10;
        std::uint16_t exponent : 5;
        std::uint16_t sign : 1;
    } s;
    std::uint16_t u;
#if defined(__aarch64__)
    __fp16 f;
#endif
    bm::uint128_t to_fixed_point() const
    {
        bm::uint128_t t = 0;
        if (s.exponent > 0) {
            // bit 10 is the implicit fraction bit made explicit
            t = (1ULL << 10ULL) | s.fraction;
            t = t << (64 - 10); // move bit 10 to bit 64
        } else {                // denorm
            std::uint64_t tt = s.fraction;
            if (tt > 0) {
                int leading_zeros = __builtin_clzll(tt);
                t = tt;
                t = t << (leading_zeros + 1);
            }
        }
        return t;
    }
};

unsigned f64_div_radix_4_iterations(const std::uint64_t numerator, const std::uint64_t denominator)
{
    fp64_t gn, gd;
    gn.u = numerator;
    gd.u = denominator;
    const unsigned max_iterations = (52 + 4) / 2; // 52b fraction + implicit 1b + guard + round + sticky
    aligned_int_t num(gn.to_fixed_point());
    aligned_int_t den(gd.to_fixed_point());
    if (num.x == 0 || den.x == 0) {
        return 0;
    }
    const unsigned radix4_iterations = srt_divide_radix_4_iterations(num, den, max_iterations);
    return radix4_iterations;
}
unsigned f64_div_radix_16_iterations(const std::uint64_t numerator, const std::uint64_t denominator)
{
    const unsigned radix4_iterations = f64_div_radix_4_iterations(numerator, denominator);
    const unsigned radix16_iterations = (radix4_iterations + 1) / 2; // round up
    DBG_SRT("f64 radix16 iterations = {}", radix16_iterations);
    return radix16_iterations;
}
unsigned f64_div_radix_64_iterations(const std::uint64_t numerator, const std::uint64_t denominator)
{
    const unsigned radix4_iterations = f64_div_radix_4_iterations(numerator, denominator);
    const unsigned radix64_iterations = (radix4_iterations + 2) / 3; // round up
    DBG_SRT("f64 radix64 iterations = {}", radix64_iterations);
    return radix64_iterations;
}

unsigned f32_div_radix_4_iterations(const std::uint32_t numerator, const std::uint32_t denominator)
{
    fp32_t gn, gd;
    gn.u = numerator;
    gd.u = denominator;
    aligned_int_t num(gn.to_fixed_point());
    aligned_int_t den(gd.to_fixed_point());
    if (num.x == 0 || den.x == 0) {
        return 0;
    }
    const unsigned max_iterations = (23 + 4 + 1) / 2; // 23 fraction + implicit 1b + guard + round + sticky, rounding up
    const unsigned radix4_iterations = srt_divide_radix_4_iterations(num, den, max_iterations);
    return radix4_iterations;
}
unsigned f32_div_radix_16_iterations(const std::uint32_t numerator, const std::uint32_t denominator)
{
    const unsigned radix4_iterations = f32_div_radix_4_iterations(numerator, denominator);
    const unsigned radix16_iterations = (radix4_iterations + 1) / 2; // round up
    DBG_SRT("f32 radix16 iterations = {}", radix16_iterations);
    return radix16_iterations;
}
unsigned f32_div_radix_64_iterations(const std::uint32_t numerator, const std::uint32_t denominator)
{
    const unsigned radix4_iterations = f32_div_radix_4_iterations(numerator, denominator);
    const unsigned radix64_iterations = (radix4_iterations + 2) / 3; // round up
    DBG_SRT("f32 radix64 iterations = {}", radix64_iterations);
    return radix64_iterations;
}

static bool upper_bits_nonzero(const bm::uint128_t& x)
{
    // test to see if any bits beyond the first integer bit are nonzero
    bm::uint128_t t = x >> 64; // remove fractional bits
    t = bit_unset(t, 0);       // turn off lsb
    return (t != 0);
}

static aligned_int_t fix_integer_alignment(const bm::uint128_t& g)
{
    aligned_int_t rv(g << 64); // create fixed point with fraction of 64 zeros
    // right shift until only the msb is integer, and the rest of the bits are fractional.
    while (upper_bits_nonzero(rv.x)) {
        rv.x = rv.x >> 1;
        rv.right_shifts++;
    }
    return rv;
}
static aligned_int_t fix_numerator_alignment(const bm::uint128_t& g)
{
    aligned_int_t rv = fix_integer_alignment(g);
    // Make the numerator purely fractional with one more shift
    rv.x = rv.x >> 1;
    rv.right_shifts++;
    return rv;
}
static aligned_int_t fix_denominator_alignment(const bm::uint128_t& g)
{
    return fix_integer_alignment(g);
}

unsigned u64_div_radix_4_iterations(const std::uint64_t numerator, const std::uint64_t denominator)
{
    const unsigned max_iterations = 64 / 2;
    const aligned_int_t nwide = fix_numerator_alignment(numerator);
    const aligned_int_t dwide = fix_denominator_alignment(denominator);
    const unsigned radix4_iterations = srt_divide_radix_4_iterations(nwide, dwide, max_iterations);
    return radix4_iterations;
}
unsigned u64_div_radix_16_iterations(const std::uint64_t numerator, const std::uint64_t denominator)
{
    const unsigned radix4_iterations = u64_div_radix_4_iterations(numerator, denominator);
    const unsigned radix16_iterations = (radix4_iterations + 1) / 2; // round up
    DBG_SRT("u64 radix16 iterations = {}", radix16_iterations);
    return radix16_iterations;
}
unsigned u64_div_radix_64_iterations(const std::uint64_t numerator, const std::uint64_t denominator)
{
    const unsigned radix4_iterations = u64_div_radix_4_iterations(numerator, denominator);
    const unsigned radix64_iterations = (radix4_iterations + 2) / 3; // round up
    DBG_SRT("u64 radix64 iterations = {}", radix64_iterations);
    return radix64_iterations;
}

unsigned u32_div_radix_4_iterations(const std::uint32_t numerator, const std::uint32_t denominator)
{
    const unsigned max_iterations = 32 / 2;
    const aligned_int_t nwide = fix_numerator_alignment(numerator);
    const aligned_int_t dwide = fix_denominator_alignment(denominator);
    const unsigned radix4_iterations = srt_divide_radix_4_iterations(nwide, dwide, max_iterations);
    return radix4_iterations;
}
unsigned u32_div_radix_16_iterations(const std::uint32_t numerator, const std::uint32_t denominator)
{
    const unsigned radix4_iterations = u32_div_radix_4_iterations(numerator, denominator);
    const unsigned radix16_iterations = (radix4_iterations + 1) / 2; // round up
    DBG_SRT("u32 radix16 iterations = {}", radix16_iterations);
    return radix16_iterations;
}
unsigned u32_div_radix_64_iterations(const std::uint32_t numerator, const std::uint32_t denominator)
{
    const unsigned radix4_iterations = u32_div_radix_4_iterations(numerator, denominator);
    const unsigned radix64_iterations = (radix4_iterations + 2) / 3; // round up
    DBG_SRT("u32 radix64 iterations = {}", radix64_iterations);
    return radix64_iterations;
}

unsigned s64_div_radix_4_iterations(const std::int64_t numerator, const std::int64_t denominator)
{
    const unsigned max_iterations = 64 / 2;
    const std::uint64_t pos_numerator = std::abs(numerator);
    const std::uint64_t pos_denominator = std::abs(denominator);
    const aligned_int_t nwide = fix_numerator_alignment(pos_numerator);
    const aligned_int_t dwide = fix_denominator_alignment(pos_denominator);
#if defined(DEBUG_SRT_DIV)
    fmt::print(stderr, "Numerator: {} {} {}\n", pos_numerator, hstr(nwide.x), emit_binary(nwide.x));
    fmt::print(stderr, "Denominator: {} {} {}\n", pos_denominator, hstr(dwide.x), emit_binary(dwide.x));
#endif
    const unsigned radix4_iterations = srt_divide_radix_4_iterations(nwide, dwide, max_iterations);
    return radix4_iterations;
}
unsigned s64_div_radix_16_iterations(const std::int64_t numerator, const std::int64_t denominator)
{
    const unsigned radix4_iterations = s64_div_radix_4_iterations(numerator, denominator);
    const unsigned radix16_iterations = (radix4_iterations + 1) / 2; // round up
    DBG_SRT("s64 radix16 iterations = {}", radix16_iterations);
    return radix16_iterations;
}
unsigned s64_div_radix_64_iterations(const std::int64_t numerator, const std::int64_t denominator)
{
    const unsigned radix4_iterations = s64_div_radix_4_iterations(numerator, denominator);
    const unsigned radix64_iterations = (radix4_iterations + 2) / 3; // round up
    DBG_SRT("s64 radix64 iterations = {}", radix64_iterations);
    return radix64_iterations;
}

unsigned s32_div_radix_4_iterations(const std::int32_t numerator, const std::int32_t denominator)
{
    const unsigned max_iterations = 32 / 2;
    const std::uint64_t pos_numerator = std::abs(numerator);
    const std::uint64_t pos_denominator = std::abs(denominator);
    const aligned_int_t nwide = fix_numerator_alignment(pos_numerator);
    const aligned_int_t dwide = fix_denominator_alignment(pos_denominator);
    const unsigned radix4_iterations = srt_divide_radix_4_iterations(nwide, dwide, max_iterations);
    return radix4_iterations;
}
unsigned s32_div_radix_16_iterations(const std::int32_t numerator, const std::int32_t denominator)
{
    const unsigned radix4_iterations = s32_div_radix_4_iterations(numerator, denominator);
    const unsigned radix16_iterations = (radix4_iterations + 1) / 2; // round up
    DBG_SRT("s32 radix16 iterations = {}", radix16_iterations);
    return radix16_iterations;
}
unsigned s32_div_radix_64_iterations(const std::int32_t numerator, const std::int32_t denominator)
{
    const unsigned radix4_iterations = s32_div_radix_4_iterations(numerator, denominator);
    const unsigned radix64_iterations = (radix4_iterations + 2) / 3; // round up
    DBG_SRT("s32 radix64 iterations = {}", radix64_iterations);
    return radix64_iterations;
}

} // namespace ampere::core::srt_div
