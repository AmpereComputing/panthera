//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
//
// Copyright (c) 2018, Ampere Computing
//
#include <memory>
#include "ampere/sc/fifo/delay_bw.h"
#include "ampere/sc/testing/test.h"

using namespace ampere::sc;
using ::testing::Return;

template<typename T = int>
struct DelayBWTestBase : public Test
{
    DelayBW<T> delay;
    DelayBWTestBase(size_t bw = 2, size_t latency = 1) : delay("delay_bw", *clock, latency, bw) {}
};

using DelayBWTest = DelayBWTestBase<>;

TEST_F(DelayBWTest, CanPutTwiceToBandwidthTwoFifo)
{
    delay.put(1);
    EXPECT_TRUE(delay.can_put());
    delay.put(2);
    EXPECT_FALSE(delay.can_put());
}

TEST_F(DelayBWTest, CanTakeTwiceFromBandwidthTwoFifo)
{
    delay.put(1);
    delay.put(2);
    tick();
    EXPECT_TRUE(delay.can_take());
    EXPECT_EQ(delay.take(), 1);
    EXPECT_TRUE(delay.can_take());
    EXPECT_EQ(delay.take(), 2);
}

TEST_F(DelayBWTest, ExceedingWriteBandwidthIsAnError)
{
    delay.put(1);
    delay.put(2);
    EXPECT_ANY_THROW(delay.put(3));
}

TEST_F(DelayBWTest, ExceedingReadBandwidthIsAnError)
{
    delay.put(1);
    delay.put(2);
    tick();
    EXPECT_EQ(delay.take(), 1);
    EXPECT_EQ(delay.take(), 2);
    EXPECT_ANY_THROW(delay.take());
}

struct DelayBWFourTest : DelayBWTestBase<>
{
    DelayBWFourTest() : DelayBWTestBase<>(4) {}
};

TEST_F(DelayBWFourTest, BandwidthFourFifoWorks)
{
    delay.put(1);
    delay.put(2);
    delay.put(3);
    delay.put(4);
    EXPECT_FALSE(delay.can_put());
    tick();
    EXPECT_EQ(delay.take(), 1);
    EXPECT_EQ(delay.take(), 2);
    EXPECT_EQ(delay.take(), 3);
    EXPECT_EQ(delay.take(), 4);
}

using DelayBWUncopyableTest = DelayBWTestBase<std::unique_ptr<int>>;

TEST_F(DelayBWUncopyableTest, CanPutUncopyableObjectInDelayBW)
{
    std::unique_ptr<int> up1{new int{1}};
    std::unique_ptr<int> up2{new int{2}};
    delay.put(std::move(up1));
    delay.put(std::move(up2));
    tick();
    std::unique_ptr<int> up3 = delay.take();
    std::unique_ptr<int> up4 = delay.take();
    EXPECT_EQ(*up3, 1);
    EXPECT_EQ(*up4, 2);
}

TEST_F(DelayBWTest, CanClearBWDelay)
{
    delay.put(4297);
    tick();
    delay.clear();
    tick();
    ASSERT_FALSE(delay.can_take());
}

TEST_F(DelayBWTest, CanPut2ItemsAndClear1)
{
    delay.put(4297);
    delay.put(4298);
    ASSERT_FALSE(delay.can_put());
    tick();
    ASSERT_TRUE(delay.can_take());
    ASSERT_EQ(4297, delay.take());
    ASSERT_TRUE(delay.can_take());
    delay.clear();
    ASSERT_TRUE(delay.can_put());
    ASSERT_FALSE(delay.can_take());
    tick();
    ASSERT_FALSE(delay.can_take());
}

struct DelayBWMultipleLatencyTest : DelayBWTestBase<>
{
    DelayBWMultipleLatencyTest() : DelayBWTestBase<>(2, 3) {}
};

TEST_F(DelayBWMultipleLatencyTest, Size)
{
    delay.put(2001);
    delay.put(1001);
    EXPECT_EQ(delay.size(), 2ul);
    tick();
    delay.put(2002);
    EXPECT_EQ(delay.size(), 3ul);
    tick();
    EXPECT_EQ(delay.size(), 3ul);
    tick();
    ASSERT_TRUE(delay.can_take());
    EXPECT_EQ(delay.take(), 2001);
    EXPECT_EQ(delay.size(), 2ul);
}

TEST_F(DelayBWMultipleLatencyTest, Ready)
{
    delay.put(2001);
    tick();
    delay.put(1001);
    tick();
    delay.put(2002);
    tick();

    EXPECT_EQ(delay.num_ready(), 1);
    tick();
    EXPECT_EQ(delay.num_ready(), 2);
    tick();
    EXPECT_EQ(delay.num_ready(), 3);
}

TEST_F(DelayBWMultipleLatencyTest, PeekAtAllEntries)
{
    delay.put(2001);
    delay.put(1001);
    tick();
    delay.put(2002);
    tick();

    EXPECT_EQ(delay.unsafe_peek(0), 2001);
    EXPECT_EQ(delay.unsafe_peek(1), 1001);
    EXPECT_EQ(delay.unsafe_peek(2), 2002);

    tick();
    ASSERT_TRUE(delay.can_take());
    EXPECT_EQ(delay.take(), 2001);
    EXPECT_EQ(delay.peek(0), 1001);
    EXPECT_EQ(delay.unsafe_peek(1), 2002);
}

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
