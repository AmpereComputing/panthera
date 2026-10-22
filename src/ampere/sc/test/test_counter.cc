//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <gtest/gtest.h>
#include "ampere/sc/counter.h"

using namespace ampere::sc;

TEST(SaturatingCounterTest, CounterOfMaxOneSaturatesProperly)
{
    SaturatingCounter counter{1};

    EXPECT_EQ(counter.get_value(), 0u);
    counter++;
    EXPECT_EQ(counter.get_value(), 1u);
    counter++;
    EXPECT_EQ(counter.get_value(), 1u);
    counter--;
    EXPECT_EQ(counter.get_value(), 0u);
    counter--;
    EXPECT_EQ(counter.get_value(), 0u);
}

TEST(SaturatingCounterTest, CounterOfMaxSixSaturatesProperly)
{
    SaturatingCounter counter{6};

    for (unsigned i = 0; i < 8; i++) {
        EXPECT_EQ(counter.get_value(), std::min(i, 6u));
        counter++;
    }
}

TEST(SaturatingCounterTest, CounterOfMinTwoSaturatesProperly)
{
    SaturatingCounter counter{2, 6, 6};

    for (int i = 6; i >= 0; i--) {
        EXPECT_EQ(counter.get_value(), std::max(static_cast<unsigned>(i), 2u));
        counter--;
    }
}

TEST(SaturatingCounterTest, CounterOfMinTwoSaturatesProperlyWrap)
{
    SaturatingCounter counter1{0, 6, 3};
    SaturatingCounter counter2{3, 6, 3};

    counter1 -= 5;
    counter2 -= 2;

    EXPECT_EQ(counter1.get_value(), 0u);
    EXPECT_EQ(counter2.get_value(), 3u);
}

TEST(SaturatingCounterTest, CounterCanBeInitialized)
{
    SaturatingCounter counter1{0, 3, 1};
    SaturatingCounter counter2{0, 3, 2};

    EXPECT_EQ(counter1.get_value(), 1u);
    EXPECT_EQ(counter2.get_value(), 2u);
}

TEST(SaturatingCounterTest, CounterDoesNotAllowOutOfBoundsInitialization)
{
    SaturatingCounter counter1{0, 3, 5};
    SaturatingCounter counter2{2, 3, 0};

    EXPECT_EQ(counter1.get_value(), 3u);
    EXPECT_EQ(counter2.get_value(), 2u);
}

TEST(SaturatingCounterTest, CanGetIndividualBit)
{
    SaturatingCounter counter{0, 8, 5};

    EXPECT_EQ(counter.get_bit(0), 1u);
    EXPECT_EQ(counter.get_bit(1), 0u);
    EXPECT_EQ(counter.get_bit(2), 1u);
}

TEST(SaturatingCounterTest, CanSetIndividualBit)
{
    SaturatingCounter counter{0, 8, 5};

    counter.set_bit(0, 0);
    counter.set_bit(1, 1);
    counter.set_bit(2, 0);

    EXPECT_EQ(counter.get_value(), 2u);
}

TEST(SaturatingCounterTest, CantSetBeyondMaxWithIndividualBit)
{
    SaturatingCounter counter{0, 4, 4};

    counter.set_bit(0, 1);
    EXPECT_EQ(counter.get_value(), 4u);

    counter.set_bit(3, 1);
    EXPECT_EQ(counter.get_value(), 4u);
}

TEST(SaturatingCounterTest, CantSetBeyondMinWithIndividualBit)
{
    SaturatingCounter counter{5, 6, 6};

    counter.set_bit(1, 0);
    EXPECT_EQ(counter.get_value(), 5u);

    counter.set_bit(3, 0);
    EXPECT_EQ(counter.get_value(), 5u);
}

TEST(SaturatingCounterTest, SignedCounter)
{
    SaturatingCounter counter{-3, 5, 0};

    counter += 9;
    EXPECT_EQ(counter.get_value(), 5);

    counter += -3;
    EXPECT_EQ(counter.get_value(), 2);

    counter += -10;
    EXPECT_EQ(counter.get_value(), -3);

    counter -= -4;
    EXPECT_EQ(counter.get_value(), 1);

    counter -= -10;
    EXPECT_EQ(counter.get_value(), 5);
}
