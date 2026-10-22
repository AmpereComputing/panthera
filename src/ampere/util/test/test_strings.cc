//
// Copyright (c) 2025 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/util/strings.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <array>
#include <string_view>
#include <utility>

using namespace std::literals;

using ampere::util::string_view_to_integer;
using ampere::util::stringified_va_args_size;
using ampere::util::stringified_va_args_to_array;

TEST(StringsTest, CountArgs)
{
    EXPECT_EQ(stringified_va_args_size("A"sv), 1U);
    EXPECT_EQ(stringified_va_args_size("A, B, C"sv), 3U);
}

TEST(StringsTest, SplitArgs)
{
    std::array expected = {
        std::make_pair("A"sv, 0U),
        std::make_pair("B"sv, 1U),
        std::make_pair("C"sv, 2U),
    };
    EXPECT_EQ(stringified_va_args_to_array<3>("A, B, C"sv), expected);
}

TEST(StringsTest, SplitArgsWithValues)
{
    std::array expected = {
        std::make_pair("A"sv, 0U),
        std::make_pair("B"sv, 3U),
        std::make_pair("C"sv, 5U),
        std::make_pair("D"sv, 6U),
        std::make_pair("E"sv, 9U),
    };
    EXPECT_EQ(stringified_va_args_to_array<5>("A, B = 3, C=5, D, E = 9"sv), expected);
}

TEST(StringsTest, StringViewToInteger)
{
    EXPECT_EQ(string_view_to_integer<unsigned>("1"sv), 1U);
    EXPECT_EQ(string_view_to_integer<unsigned>("12"sv), 12U);
    EXPECT_EQ(string_view_to_integer<unsigned>("123"sv), 123U);
    EXPECT_EQ(string_view_to_integer<unsigned>("1234"sv), 1234U);

    EXPECT_EQ(string_view_to_integer<int>("1234"sv), 1234);
    EXPECT_EQ(string_view_to_integer<int>("-1234"sv), -1234);

    EXPECT_EQ(string_view_to_integer<unsigned>("0xbeef"sv), 0xbeefU);
    EXPECT_EQ(string_view_to_integer<unsigned long>("0x6179beef"sv), 0x6179beefUL);
    EXPECT_EQ(string_view_to_integer<unsigned long>("0x6179BEEF"sv), 0x6179beefUL);

    EXPECT_EQ(string_view_to_integer<unsigned>("0b0"sv), 0b0U);
    EXPECT_EQ(string_view_to_integer<unsigned>("0b1"sv), 0b1U);
    EXPECT_EQ(string_view_to_integer<unsigned>("0b10"sv), 0b10U);
    EXPECT_EQ(string_view_to_integer<unsigned>("0b101"sv), 0b101U);
    EXPECT_EQ(string_view_to_integer<unsigned>("0b0110"sv), 0b0110U);
}

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
