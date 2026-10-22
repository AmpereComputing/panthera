//
// Copyright (c) 2024-2025 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/util/arrays.h"
#include "ampere/util/enum.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <optional>
#include <nlohmann/json_fwd.hpp>
#include <sstream>
#include <stdexcept>
#include <string>
#include <fmt/format.h>

template<typename E>
struct EnumAttributes : public ampere::util::EnumerationDefinition<E>
{
    using ampere::util::EnumerationDefinition<E>::has_values;
    using ampere::util::EnumerationDefinition<E>::offset;
    using ampere::util::EnumerationDefinition<E>::uniformity;
};

// Basic enum without values
make_enum(MYENUM,
    ONE,    // comments
    TWO,    // on values
    THREE,  // still
    FOUR    // work
);
static_assert(!EnumAttributes<MYENUM>::has_values);
static_assert(EnumAttributes<MYENUM>::offset == 0);
static_assert(EnumAttributes<MYENUM>::uniformity == ampere::util::RangeUniformity::INCREMENTAL);

// Enum with integer values
make_enum(MYENUM_VALUES,
    CAT = 2, // values
    DOG = 6, // also still
    BAT = 9  // work
);
static_assert(EnumAttributes<MYENUM_VALUES>::has_values);
static_assert(EnumAttributes<MYENUM_VALUES>::offset == 2);
static_assert(EnumAttributes<MYENUM_VALUES>::uniformity == ampere::util::RangeUniformity::INCREMENTAL);

// Enum with hex values
make_enum(MYENUM_HEX_VALUES,
    ANT = 0x23,   // hex values
    RAT = 0xf0A,  // also still
    COW = 0xfff3  // work
);
static_assert(EnumAttributes<MYENUM_HEX_VALUES>::has_values);
static_assert(EnumAttributes<MYENUM_HEX_VALUES>::offset == 0x23);
static_assert(EnumAttributes<MYENUM_HEX_VALUES>::uniformity == ampere::util::RangeUniformity::NONE);

// Enum with only some values specified
make_enum(MYENUM_PARTIAL_VALUES,
    FOX, // 0
    PIG, // 1
    YAK = 9,
    OWL, // 10
    ELK, // 11
    APE = 5,
    EMU // 6
);
static_assert(EnumAttributes<MYENUM_PARTIAL_VALUES>::has_values);
static_assert(EnumAttributes<MYENUM_PARTIAL_VALUES>::offset == 0);
static_assert(EnumAttributes<MYENUM_PARTIAL_VALUES>::uniformity == ampere::util::RangeUniformity::INCREMENTAL);

// Enum with incremental values, but non-zero offset
make_enum(MYENUM_INCR_VALUES,
    TIGER = 7,
    PANTHER = 8,
    CHEETAH = 9,
    LION = 10
);
static_assert(EnumAttributes<MYENUM_INCR_VALUES>::has_values);
static_assert(EnumAttributes<MYENUM_INCR_VALUES>::offset == 7);
static_assert(EnumAttributes<MYENUM_INCR_VALUES>::uniformity == ampere::util::RangeUniformity::INCREMENTAL);

// Enum with exponential values, and non-zero offset
make_enum(MYENUM_EXP_VALUES,
    JACKAL = 0b100,
    WOLF   = 0b1000,
    COYOTE = 0b10000,
    FOX =    0b100000
);
static_assert(EnumAttributes<MYENUM_EXP_VALUES>::has_values);
static_assert(EnumAttributes<MYENUM_EXP_VALUES>::offset == 0b100);
static_assert(EnumAttributes<MYENUM_EXP_VALUES>::uniformity == ampere::util::RangeUniformity::EXPONENTIAL);

using namespace std::literals;

using ampere::util::each_enum;
using ampere::util::enum_size;
using ampere::util::enum_to_string;
using ampere::util::has_enum_definition_v;
using ampere::util::string_to_enum;

using ::testing::ElementsAre;

struct EnumTest : ::testing::Test
{
    MYENUM x{};
};

enum class PlainEnum {
    XXX,
    YYY,
    ZZZ
};

template<typename E>
auto to_underlying(E e)
{
    return static_cast<std::underlying_type_t<E>>(e);
}

TEST_F(EnumTest, HasDefinition)
{
    EXPECT_TRUE(has_enum_definition_v<MYENUM>);
    EXPECT_TRUE(has_enum_definition_v<MYENUM_VALUES>);
    EXPECT_TRUE(has_enum_definition_v<MYENUM_HEX_VALUES>);
    EXPECT_TRUE(has_enum_definition_v<MYENUM_PARTIAL_VALUES>);
    EXPECT_FALSE(has_enum_definition_v<PlainEnum>);
}

TEST_F(EnumTest, Initialize)
{
    EXPECT_EQ(x, MYENUM::ONE);
}

TEST_F(EnumTest, InitializeFromString)
{
    EXPECT_EQ(string_to_enum<MYENUM>("ONE"), MYENUM::ONE);
    EXPECT_EQ(string_to_enum<MYENUM>("TWO"), MYENUM::TWO);
    EXPECT_EQ(string_to_enum<MYENUM>("THREE"), MYENUM::THREE);
    EXPECT_EQ(string_to_enum<MYENUM>("FOUR"), MYENUM::FOUR);

    EXPECT_EQ(string_to_enum<MYENUM_VALUES>("CAT"), MYENUM_VALUES::CAT);
    EXPECT_EQ(string_to_enum<MYENUM_VALUES>("DOG"), MYENUM_VALUES::DOG);
    EXPECT_EQ(string_to_enum<MYENUM_VALUES>("BAT"), MYENUM_VALUES::BAT);

    EXPECT_EQ(string_to_enum<MYENUM_HEX_VALUES>("ANT"), MYENUM_HEX_VALUES::ANT);
    EXPECT_EQ(string_to_enum<MYENUM_HEX_VALUES>("RAT"), MYENUM_HEX_VALUES::RAT);
    EXPECT_EQ(string_to_enum<MYENUM_HEX_VALUES>("COW"), MYENUM_HEX_VALUES::COW);

    EXPECT_EQ(string_to_enum<MYENUM_PARTIAL_VALUES>("FOX"), MYENUM_PARTIAL_VALUES::FOX);
    EXPECT_EQ(string_to_enum<MYENUM_PARTIAL_VALUES>("PIG"), MYENUM_PARTIAL_VALUES::PIG);
    EXPECT_EQ(string_to_enum<MYENUM_PARTIAL_VALUES>("YAK"), MYENUM_PARTIAL_VALUES::YAK);
    EXPECT_EQ(string_to_enum<MYENUM_PARTIAL_VALUES>("OWL"), MYENUM_PARTIAL_VALUES::OWL);
    EXPECT_EQ(string_to_enum<MYENUM_PARTIAL_VALUES>("ELK"), MYENUM_PARTIAL_VALUES::ELK);
    EXPECT_EQ(string_to_enum<MYENUM_PARTIAL_VALUES>("APE"), MYENUM_PARTIAL_VALUES::APE);
    EXPECT_EQ(string_to_enum<MYENUM_PARTIAL_VALUES>("EMU"), MYENUM_PARTIAL_VALUES::EMU);

    EXPECT_EQ(string_to_enum<MYENUM_INCR_VALUES>("TIGER"), MYENUM_INCR_VALUES::TIGER);
    EXPECT_EQ(string_to_enum<MYENUM_INCR_VALUES>("PANTHER"), MYENUM_INCR_VALUES::PANTHER);
    EXPECT_EQ(string_to_enum<MYENUM_INCR_VALUES>("CHEETAH"), MYENUM_INCR_VALUES::CHEETAH);
    EXPECT_EQ(string_to_enum<MYENUM_INCR_VALUES>("LION"), MYENUM_INCR_VALUES::LION);

    EXPECT_EQ(string_to_enum<MYENUM_EXP_VALUES>("JACKAL"), MYENUM_EXP_VALUES::JACKAL);
    EXPECT_EQ(string_to_enum<MYENUM_EXP_VALUES>("WOLF"), MYENUM_EXP_VALUES::WOLF);
    EXPECT_EQ(string_to_enum<MYENUM_EXP_VALUES>("COYOTE"), MYENUM_EXP_VALUES::COYOTE);
    EXPECT_EQ(string_to_enum<MYENUM_EXP_VALUES>("FOX"), MYENUM_EXP_VALUES::FOX);
}

TEST_F(EnumTest, Missing)
{
    EXPECT_THROW({ string_to_enum<MYENUM>("FIVE"); }, std::out_of_range);
    EXPECT_THROW({ string_to_enum<MYENUM_VALUES>("COW"); }, std::out_of_range);
    EXPECT_THROW({ string_to_enum<MYENUM_HEX_VALUES>("BAT"); }, std::out_of_range);
    EXPECT_THROW({ string_to_enum<MYENUM_PARTIAL_VALUES>("ANT"); }, std::out_of_range);
    EXPECT_THROW({ string_to_enum<MYENUM_INCR_VALUES>("CAT"); }, std::out_of_range);
    EXPECT_THROW({ string_to_enum<MYENUM_EXP_VALUES>("DOG"); }, std::out_of_range);
}

TEST_F(EnumTest, ToString)
{
    EXPECT_EQ(enum_to_string<MYENUM>(MYENUM::ONE), "ONE"sv);
    EXPECT_EQ(enum_to_string<MYENUM>(MYENUM::TWO), "TWO"sv);
    EXPECT_EQ(enum_to_string<MYENUM>(MYENUM::THREE), "THREE"sv);
    EXPECT_EQ(enum_to_string<MYENUM>(MYENUM::FOUR), "FOUR"sv);

    EXPECT_EQ(enum_to_string<MYENUM_VALUES>(MYENUM_VALUES::CAT), "CAT"sv);
    EXPECT_EQ(enum_to_string<MYENUM_VALUES>(MYENUM_VALUES::DOG), "DOG"sv);
    EXPECT_EQ(enum_to_string<MYENUM_VALUES>(MYENUM_VALUES::BAT), "BAT"sv);

    EXPECT_EQ(enum_to_string<MYENUM_HEX_VALUES>(MYENUM_HEX_VALUES::ANT), "ANT"sv);
    EXPECT_EQ(enum_to_string<MYENUM_HEX_VALUES>(MYENUM_HEX_VALUES::RAT), "RAT"sv);
    EXPECT_EQ(enum_to_string<MYENUM_HEX_VALUES>(MYENUM_HEX_VALUES::COW), "COW"sv);

    EXPECT_EQ(enum_to_string<MYENUM_PARTIAL_VALUES>(MYENUM_PARTIAL_VALUES::FOX), "FOX"sv);
    EXPECT_EQ(enum_to_string<MYENUM_PARTIAL_VALUES>(MYENUM_PARTIAL_VALUES::PIG), "PIG"sv);
    EXPECT_EQ(enum_to_string<MYENUM_PARTIAL_VALUES>(MYENUM_PARTIAL_VALUES::YAK), "YAK"sv);
    EXPECT_EQ(enum_to_string<MYENUM_PARTIAL_VALUES>(MYENUM_PARTIAL_VALUES::OWL), "OWL"sv);
    EXPECT_EQ(enum_to_string<MYENUM_PARTIAL_VALUES>(MYENUM_PARTIAL_VALUES::ELK), "ELK"sv);
    EXPECT_EQ(enum_to_string<MYENUM_PARTIAL_VALUES>(MYENUM_PARTIAL_VALUES::APE), "APE"sv);
    EXPECT_EQ(enum_to_string<MYENUM_PARTIAL_VALUES>(MYENUM_PARTIAL_VALUES::EMU), "EMU"sv);

    EXPECT_EQ(enum_to_string<MYENUM_INCR_VALUES>(MYENUM_INCR_VALUES::TIGER), "TIGER"sv);
    EXPECT_EQ(enum_to_string<MYENUM_INCR_VALUES>(MYENUM_INCR_VALUES::PANTHER), "PANTHER"sv);
    EXPECT_EQ(enum_to_string<MYENUM_INCR_VALUES>(MYENUM_INCR_VALUES::CHEETAH), "CHEETAH"sv);
    EXPECT_EQ(enum_to_string<MYENUM_INCR_VALUES>(MYENUM_INCR_VALUES::LION), "LION"sv);

    EXPECT_EQ(enum_to_string<MYENUM_EXP_VALUES>(MYENUM_EXP_VALUES::JACKAL), "JACKAL"sv);
    EXPECT_EQ(enum_to_string<MYENUM_EXP_VALUES>(MYENUM_EXP_VALUES::WOLF), "WOLF"sv);
    EXPECT_EQ(enum_to_string<MYENUM_EXP_VALUES>(MYENUM_EXP_VALUES::COYOTE), "COYOTE"sv);
    EXPECT_EQ(enum_to_string<MYENUM_EXP_VALUES>(MYENUM_EXP_VALUES::FOX), "FOX"sv);
}

TEST_F(EnumTest, AsValue)
{
    EXPECT_EQ(to_underlying(MYENUM::ONE), 0U);
    EXPECT_EQ(to_underlying(MYENUM::TWO), 1U);
    EXPECT_EQ(to_underlying(MYENUM::THREE), 2U);
    EXPECT_EQ(to_underlying(MYENUM::FOUR), 3U);

    EXPECT_EQ(to_underlying(MYENUM_VALUES::CAT), 2U);
    EXPECT_EQ(to_underlying(MYENUM_VALUES::DOG), 6U);
    EXPECT_EQ(to_underlying(MYENUM_VALUES::BAT), 9U);

    EXPECT_EQ(to_underlying(MYENUM_HEX_VALUES::ANT), 0x23);
    EXPECT_EQ(to_underlying(MYENUM_HEX_VALUES::RAT), 0xf0A);
    EXPECT_EQ(to_underlying(MYENUM_HEX_VALUES::COW), 0xfff3);

    EXPECT_EQ(to_underlying(MYENUM_PARTIAL_VALUES::FOX), 0U);
    EXPECT_EQ(to_underlying(MYENUM_PARTIAL_VALUES::PIG), 1U);
    EXPECT_EQ(to_underlying(MYENUM_PARTIAL_VALUES::YAK), 9U);
    EXPECT_EQ(to_underlying(MYENUM_PARTIAL_VALUES::OWL), 10U);
    EXPECT_EQ(to_underlying(MYENUM_PARTIAL_VALUES::ELK), 11U);
    EXPECT_EQ(to_underlying(MYENUM_PARTIAL_VALUES::APE), 5U);
    EXPECT_EQ(to_underlying(MYENUM_PARTIAL_VALUES::EMU), 6U);

    EXPECT_EQ(to_underlying(MYENUM_INCR_VALUES::TIGER), 7U);
    EXPECT_EQ(to_underlying(MYENUM_INCR_VALUES::PANTHER), 8U);
    EXPECT_EQ(to_underlying(MYENUM_INCR_VALUES::CHEETAH), 9U);
    EXPECT_EQ(to_underlying(MYENUM_INCR_VALUES::LION), 10U);

    EXPECT_EQ(to_underlying(MYENUM_EXP_VALUES::JACKAL), 0b100);
    EXPECT_EQ(to_underlying(MYENUM_EXP_VALUES::WOLF), 0b1000);
    EXPECT_EQ(to_underlying(MYENUM_EXP_VALUES::COYOTE), 0b10000);
    EXPECT_EQ(to_underlying(MYENUM_EXP_VALUES::FOX), 0b100000);
}

TEST_F(EnumTest, Size)
{
    EXPECT_EQ(enum_size<MYENUM>(), 4U);
    EXPECT_EQ(enum_size<MYENUM_VALUES>(), 3U);
    EXPECT_EQ(enum_size<MYENUM_HEX_VALUES>(), 3U);
    EXPECT_EQ(enum_size<MYENUM_PARTIAL_VALUES>(), 7U);
    EXPECT_EQ(enum_size<MYENUM_INCR_VALUES>(), 4U);
    EXPECT_EQ(enum_size<MYENUM_EXP_VALUES>(), 4U);
}

TEST_F(EnumTest, Each)
{
    auto e = each_enum<MYENUM>();
    std::vector<MYENUM> values(e.begin(), e.end());
    EXPECT_THAT(values, ElementsAre(MYENUM::ONE, MYENUM::TWO, MYENUM::THREE, MYENUM::FOUR));
}

TEST_F(EnumTest, EachWithValues)
{
    auto e = each_enum<MYENUM_VALUES>();
    std::vector<MYENUM_VALUES> values(e.begin(), e.end());
    EXPECT_THAT(values, ElementsAre(MYENUM_VALUES::CAT, MYENUM_VALUES::DOG, MYENUM_VALUES::BAT));
}

TEST_F(EnumTest, Stream)
{
    std::stringstream s;
    s << MYENUM::THREE;
    EXPECT_EQ(s.str(), "THREE");
}

TEST_F(EnumTest, StreamOptional)
{
    std::stringstream s;
    s << std::optional<MYENUM>();
    EXPECT_EQ(s.str(), "none");

    s = {};
    s << std::make_optional(MYENUM::THREE);
    EXPECT_EQ(s.str(), "THREE");
}

TEST_F(EnumTest, Format)
{
    EXPECT_EQ(fmt::format("{}", MYENUM::FOUR), "FOUR");
}

TEST_F(EnumTest, FormatOptional)
{
    EXPECT_EQ(fmt::format("{}", std::optional<MYENUM>()), "none");
    EXPECT_EQ(fmt::format("{}", std::make_optional(MYENUM::FOUR)), "FOUR");
}

TEST_F(EnumTest, FromJSONNumber)
{
    EXPECT_EQ(nlohmann::json(0).get<MYENUM>(), MYENUM::ONE);
    EXPECT_EQ(nlohmann::json(1).get<MYENUM>(), MYENUM::TWO);
}

TEST_F(EnumTest, FromJSONNumberString)
{
    EXPECT_EQ(nlohmann::json("0").get<MYENUM>(), MYENUM::ONE);
    EXPECT_EQ(nlohmann::json("1").get<MYENUM>(), MYENUM::TWO);
}

TEST_F(EnumTest, FromJSONString)
{
    EXPECT_EQ(nlohmann::json("ONE").get<MYENUM>(), MYENUM::ONE);
    EXPECT_EQ(nlohmann::json("TWO").get<MYENUM>(), MYENUM::TWO);
}

TEST_F(EnumTest, FromJSONGetTo)
{
    MYENUM myenum;
    nlohmann::json("ONE").get_to(myenum);
    EXPECT_EQ(myenum, MYENUM::ONE);
}

TEST_F(EnumTest, ToJSON)
{
    auto json_enum = nlohmann::json(MYENUM::ONE);
    EXPECT_EQ(json_enum.dump(), "\"ONE\"");
    EXPECT_EQ(json_enum.get<MYENUM>(), MYENUM::ONE);
}

struct NonConstantEnumTest : ::testing::Test
{
    define_non_constant_enum(MYDYANMICENUM);

    static auto build()
    {
        auto builder = MYDYANMICENUM::create();
        builder.add("ONE");
        builder.add("TWO");
        builder.add("THREE");
        builder.add("FIVE", 5);
        builder.add("SIX");
        return builder;
    }

    void SetUp() override { MYDYANMICENUM::finalize(build()); }
};

struct EnumerationBuilderTest : NonConstantEnumTest
{
    void SetUp() override {}
};

TEST_F(EnumerationBuilderTest, AlreadyAddedValue)
{
    auto builder = MYDYANMICENUM::create();
    builder.add("ONE", 1);
    EXPECT_THROW({ builder.add("TWO", 1); }, std::logic_error);
}

TEST_F(EnumerationBuilderTest, AlreadyAddedName)
{
    auto builder = MYDYANMICENUM::create();
    builder.add("ONE", 1);
    EXPECT_THROW({ builder.add("ONE"); }, std::logic_error);
}

TEST_F(NonConstantEnumTest, Size)
{
    EXPECT_EQ(MYDYANMICENUM::size(), 5U);
}

TEST_F(NonConstantEnumTest, Missing)
{
    EXPECT_THROW({ MYDYANMICENUM("SEVEN"sv); }, std::out_of_range);
}

TEST_F(NonConstantEnumTest, Stream)
{
    std::stringstream s;
    s << MYDYANMICENUM(2);
    EXPECT_EQ(s.str(), "THREE");
}

TEST_F(NonConstantEnumTest, Format)
{
    EXPECT_EQ(fmt::format("{}", MYDYANMICENUM(5)), "FIVE");
}

TEST_F(NonConstantEnumTest, ToString)
{
    EXPECT_EQ(std::string_view(MYDYANMICENUM(1)), "TWO"sv);
}

TEST_F(NonConstantEnumTest, ExtendNames)
{
    define_non_constant_enum(SOMEOTHERDYNAMICENUM);

    MYDYANMICENUM::finalize(build());
    auto builder = SOMEOTHERDYNAMICENUM::create();
    builder.add("ZERO", 0);
    builder.extend_names<MYDYANMICENUM>();
    SOMEOTHERDYNAMICENUM::finalize(std::move(builder));

    EXPECT_EQ(SOMEOTHERDYNAMICENUM::size(), 6);
    EXPECT_EQ(SOMEOTHERDYNAMICENUM("ONE"sv).underlying(), 1);
}

TEST_F(NonConstantEnumTest, ExtendValues)
{
    define_non_constant_enum(ANOTHERDYNAMICENUM);

    MYDYANMICENUM::finalize(build());
    auto builder = ANOTHERDYNAMICENUM::create();
    builder.add("SEVEN", 7);
    builder.extend_values<MYDYANMICENUM>();
    ANOTHERDYNAMICENUM::finalize(std::move(builder));

    EXPECT_EQ(ANOTHERDYNAMICENUM::size(), 6);
    EXPECT_EQ(ANOTHERDYNAMICENUM("ONE"sv).underlying(), 0);
}

TEST_F(NonConstantEnumTest, ExtendSubset)
{
    define_non_constant_enum(YETANOTHERDYNAMICENUM);

    MYDYANMICENUM::finalize(build());
    auto builder = YETANOTHERDYNAMICENUM::create();
    builder.add("SEVEN", 7);
    builder.extend_names<MYDYANMICENUM>(MYDYANMICENUM("TWO"sv), MYDYANMICENUM("FIVE"sv));
    YETANOTHERDYNAMICENUM::finalize(std::move(builder));

    EXPECT_EQ(YETANOTHERDYNAMICENUM::size(), 4);
    EXPECT_EQ(YETANOTHERDYNAMICENUM("TWO"sv).underlying(), 8);
}

struct StaticEnumerationTest : NonConstantEnumTest
{
    MYDYANMICENUM::enumeration_type my_enum;

    void SetUp() override { my_enum = build(); }
};

TEST_F(StaticEnumerationTest, Size)
{
    EXPECT_EQ(my_enum.size(), 5);
}

TEST_F(StaticEnumerationTest, GetString)
{
    EXPECT_EQ(std::string(my_enum.at(0)), "ONE");
    EXPECT_EQ(std::string(my_enum.at(2)), "THREE");
    EXPECT_EQ(std::string(my_enum.at(5)), "FIVE");
    EXPECT_EQ(std::string(my_enum.at(6)), "SIX");
}

struct FrozenEnumerationTest : ::testing::Test
{
    ampere::util::FrozenEnumeration<> frozen_enum;

    void SetUp() override
    {
        auto builder = ampere::util::FrozenEnumeration<>::create();
        builder.add("ONE");
        builder.add("TWO", 2);
        builder.add("THREE");
        frozen_enum = builder.freeze();
    }
};

TEST_F(FrozenEnumerationTest, Size)
{
    EXPECT_EQ(frozen_enum.size(), 3);
}

TEST_F(FrozenEnumerationTest, GetValue)
{
    EXPECT_EQ(static_cast<std::string_view>(frozen_enum.at(0)), "ONE"sv);
    EXPECT_EQ(static_cast<int>(frozen_enum.at(2)), 2);
}

TEST_F(FrozenEnumerationTest, GetValueByName)
{
    EXPECT_EQ(static_cast<std::string_view>(frozen_enum.at("TWO")), "TWO"sv);
    EXPECT_EQ(static_cast<int>(frozen_enum.at("THREE")), 3);
}

TEST_F(FrozenEnumerationTest, GetMissingValue)
{
    EXPECT_THROW({ frozen_enum.at(1); }, std::logic_error);
}

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
