//
// Copyright (c) 2024 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include <gtest/gtest.h>
#include "ixu_srt_div.h"
using namespace ampere::core::srt_div;
TEST(srt_div, test_vectors)
{
    ASSERT_EQ(f64_div_radix_16_iterations(0x3ffe0000'00000000, 0x3ff80000'00000000), 1U);
    ASSERT_EQ(f64_div_radix_16_iterations(0x3fffe000'00000000, 0x3ff80000'00000000), 2U);
    ASSERT_EQ(f64_div_radix_16_iterations(0x3ffffe00'00000000, 0x3ff80000'00000000), 3U);
    ASSERT_EQ(f64_div_radix_16_iterations(0x3fffffe0'00000000, 0x3ff80000'00000000), 4U);
    ASSERT_EQ(f64_div_radix_16_iterations(0x3ffffffe'00000000, 0x3ff80000'00000000), 5U);
    ASSERT_EQ(f64_div_radix_16_iterations(0x3ffffffe'00000000, 0x3ff80000'00000000), 5U);
    ASSERT_EQ(f64_div_radix_16_iterations(0x3fffffff'e0000000, 0x3ff80000'00000000), 6U);
    ASSERT_EQ(f64_div_radix_16_iterations(0x3fffffff'fe000000, 0x3ff80000'00000000), 7U);
    ASSERT_EQ(f64_div_radix_16_iterations(0x3fffffff'ffe00000, 0x3ff80000'00000000), 8U);
    ASSERT_EQ(f64_div_radix_16_iterations(0x3fffffff'fffe0000, 0x3ff80000'00000000), 9U);
    ASSERT_EQ(f64_div_radix_16_iterations(0x3fffffff'ffffe000, 0x3ff80000'00000000), 10U);
    ASSERT_EQ(f64_div_radix_16_iterations(0x3fffffff'fffffe00, 0x3ff80000'00000000), 11U);
    ASSERT_EQ(f64_div_radix_16_iterations(0x3fffffff'ffffffe0, 0x3ff80000'00000000), 12U);
    ASSERT_EQ(f64_div_radix_16_iterations(0x3fffffff'fffffffe, 0x3ff80000'00000000), 13U);
    ASSERT_EQ(f64_div_radix_16_iterations(0x3fffffff'ffffffff, 0x3ff80000'00000000), 14U);
}

