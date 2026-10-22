//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#pragma once
#include <cstdint>

// not using report.h or AMPERE_ERROR() because also used by tracing_tools
namespace ampere::archx {

constexpr unsigned MAXVL = 16;
constexpr unsigned MAXVL_BITS = (MAXVL * 8);

union vector_t
{
    std::uint64_t u64[MAXVL / sizeof(std::uint64_t)];
    std::uint32_t u32[MAXVL / sizeof(std::uint32_t)];
    std::uint16_t u16[MAXVL / sizeof(std::uint16_t)];
    std::uint8_t u8[MAXVL / sizeof(std::uint8_t)];
    std::int64_t i64[MAXVL / sizeof(std::int64_t)];
    std::int32_t i32[MAXVL / sizeof(std::int32_t)];
    std::int16_t i16[MAXVL / sizeof(std::int16_t)];
    std::int8_t i8[MAXVL / sizeof(std::int8_t)];
    double f64[MAXVL / sizeof(double)];
    float f32[MAXVL / sizeof(float)];

    std::uint64_t get_vector_element_u64(unsigned elem, unsigned size) const
    {
        switch (size) {
        case 64:
            return u64[elem];
        case 32:
            return u32[elem];
        case 16:
            return u16[elem];
        case 8:
            return u8[elem];
        default:
            return 0;
        }
    }

    std::int64_t get_vector_element_i64(unsigned elem, unsigned size) const
    {
        switch (size) {
        case 64:
            return i64[elem];
        case 32:
            return i32[elem];
        case 16:
            return i16[elem];
        case 8:
            return i8[elem];
        default:
            return 0;
        }
    }
};

} // namespace ampere::archx
