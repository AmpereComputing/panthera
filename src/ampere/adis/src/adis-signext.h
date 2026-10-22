//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once
#include <cstdint>
namespace adis {

static std::uint32_t sign_extend(std::uint32_t x, std::uint32_t bit)
{
    std::uint32_t  m = 1U << (bit - 1);
    return (x ^ m) - m;
}

static inline std::int32_t sign_extend9(std::uint32_t x) {
    return sign_extend(x,9);
}
static inline std::int32_t sign_extend10(std::uint32_t x) {
    return sign_extend(x,10);
}
static inline std::int32_t sign_extend14(std::uint32_t x) {
    return sign_extend(x,14);
}
static inline std::int32_t sign_extend19(std::uint32_t x) {
    return sign_extend(x,19);
}
static inline std::int32_t sign_extend21(std::uint32_t x) {
    return sign_extend(x,21);
}
static inline std::int32_t sign_extend26(std::uint32_t x) {
    return sign_extend(x,26);
}
static inline std::int64_t sign_extend32(std::uint32_t x) {
    return static_cast<std::int64_t>(static_cast<std::int32_t>(x));
}
}
