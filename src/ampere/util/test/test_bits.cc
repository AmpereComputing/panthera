//
// Copyright (c) 2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include <limits>
#include <gtest/gtest.h>
#include "ampere/util/bits.h"

using namespace ampere::util;

TEST(TestBits, TestBitsize)
{
    EXPECT_EQ(bitsize(0), 0);
    EXPECT_EQ(bitsize(1), 1);
    EXPECT_EQ(bitsize(0b10), 2);
    EXPECT_EQ(bitsize(0b11), 2);
    EXPECT_EQ(bitsize(0b10110), 5);
    EXPECT_EQ(bitsize(0xfffffffffffffffful), 64);
}

TEST(TestBits, TestLog2)
{
    EXPECT_ANY_THROW(log2(0));
    EXPECT_EQ(log2(1), 0);
    EXPECT_EQ(log2(2), 1);
    EXPECT_EQ(log2(3), 1);
    EXPECT_EQ(log2(4), 2);
    EXPECT_EQ(log2(7), 2);
    EXPECT_EQ(log2(8), 3);
    EXPECT_EQ(log2(std::numeric_limits<std::uint64_t>::max()), 63);
}

TEST(TestBits, TestMask)
{
    EXPECT_EQ(mask(0), 0u);
    EXPECT_EQ(mask(1), 1u);
    EXPECT_EQ(mask(2), 0b11u);
    EXPECT_EQ(mask(3), 0b111u);
    EXPECT_EQ(mask(17), 0b11111111111111111u);
    EXPECT_EQ(mask(63), 0x7ffffffffffffffful);
    EXPECT_EQ(mask(64), 0xfffffffffffffffful);
    EXPECT_THROW({ mask(65); }, std::invalid_argument);
    EXPECT_THROW(mask(-3), std::invalid_argument);
}

TEST(TestBits, TestMaskRange)
{
    EXPECT_EQ(mask(0, 0), 0b1u);
    EXPECT_EQ(mask(1, 1), 0b10u);
    EXPECT_EQ(mask(7, 7), 0b10000000u);
    EXPECT_EQ(mask(7, 0), 0b11111111u);
    EXPECT_EQ(mask(5, 2), 0b111100u);
    EXPECT_EQ(mask(63, 0), 0xfffffffffffffffful);
    EXPECT_THROW({ mask(64, 0); }, std::invalid_argument);
    EXPECT_THROW({ mask(63, -1); }, std::invalid_argument);
    EXPECT_THROW({ mask(-4, -5); }, std::invalid_argument);
    EXPECT_THROW({ mask(3, 7); }, std::invalid_argument);
}

TEST(TestBits, TestIsPowerOfTwo)
{
    EXPECT_TRUE(is_power_of_two(1));
    EXPECT_TRUE(is_power_of_two(2));
    EXPECT_TRUE(is_power_of_two(4));
    EXPECT_TRUE(is_power_of_two(8));
    EXPECT_TRUE(is_power_of_two(1ul << 32));
    EXPECT_TRUE(is_power_of_two(1ul << 33));
    EXPECT_TRUE(is_power_of_two(1ul << 63));
    EXPECT_FALSE(is_power_of_two(3));
    EXPECT_FALSE(is_power_of_two(7));
    EXPECT_FALSE(is_power_of_two(12));
    EXPECT_FALSE(is_power_of_two(5ul << 32));
    EXPECT_THROW(is_power_of_two(0), std::invalid_argument);
}
