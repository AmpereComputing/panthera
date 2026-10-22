//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once
#include <cstdint>
#include "ampere/sc/report.h"

namespace ampere::div_supp {

static inline std::uint64_t count_bits_i64(std::int64_t x)
{
    std::uint64_t m = 64 - __builtin_clrsbl(x); // count redundant leading sign bits
    AMPERE_ASSERT(m <= 64, "count_bits_i64()");
    return m;
}
static inline std::uint64_t count_bits_i32(std::int32_t x)
{
    std::uint64_t m = 32 - __builtin_clrsb(x); // count redundant leading sign bits
    AMPERE_ASSERT(m <= 32, "count_bits_i32()");
    return m;
}
static inline std::uint64_t count_bits_u64(std::uint64_t x)
{
    std::uint64_t m = 64 - __builtin_clzl(x); // count leading zeros
    AMPERE_ASSERT(m <= 64, "count_bits_u64()");
    return m;
}
static inline std::uint64_t count_bits_u32(std::uint32_t x)
{
    std::uint64_t m = 32 - __builtin_clz(x); // count leading zeros
    AMPERE_ASSERT(m <= 32, "count_bits_u32(): 32-lz={} from x={:08x} lz={} ", m, x, __builtin_clzl(x));
    return m;
}

} // namespace ampere::div_supp
