//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/sc/mimo/broadcast.h"
#include "ampere/sc/testing/test.h"
#include <gtest/gtest.h>

namespace ampere {
namespace sc {

using ::testing::Return;

class BroadcastTest : public Test
{};

TEST_F(BroadcastTest, CantReadFromEmptyFifo)
{
    Broadcast<int> broadcast("broadcast", *clock, 1);
    EXPECT_FALSE(broadcast.can_read());
}

TEST_F(BroadcastTest, CanWriteToEmptyFifo)
{
    Broadcast<int> broadcast("broadcast", *clock, 1);
    EXPECT_TRUE(broadcast.can_put());
    broadcast.put(42);
}

TEST_F(BroadcastTest, CantWriteToFullFifo)
{
    Broadcast<int> broadcast("broadcast", *clock, 1);
    broadcast.put(2001);
    EXPECT_FALSE(broadcast.can_put());
}

TEST_F(BroadcastTest, PuttingToAFullFifoIsAnError)
{
    Broadcast<int> broadcast("broadcast", *clock, 1);
    broadcast.put(2001);
    EXPECT_ANY_THROW(broadcast.put(2002));
}

TEST_F(BroadcastTest, CanWriteWhileEntryInBroadcast)
{
    Broadcast<int> broadcast("broadcast", *clock, 2);
    broadcast.put(2029);
    tick();
    EXPECT_TRUE(broadcast.can_put());
    broadcast.put(2030);
    tick();
    EXPECT_TRUE(broadcast.can_read());
    EXPECT_EQ(2029, broadcast.read());
    tick();
    EXPECT_TRUE(broadcast.can_read());
    EXPECT_EQ(2030, broadcast.read());
}

TEST_F(BroadcastTest, CanReadFromLatencyOneFifoInOneCycle)
{
    Broadcast<int> broadcast("broadcast", *clock, 1);
    broadcast.put(42);
    EXPECT_FALSE(broadcast.can_read());
    tick();
    ASSERT_TRUE(broadcast.can_read());
    EXPECT_EQ(broadcast.read(), 42);
}

TEST_F(BroadcastTest, CanReadFromLatencyZeroFifoSameCycle)
{
    Broadcast<int> broadcast("broadcast", *clock, 0);
    broadcast.put(42);
    ASSERT_TRUE(broadcast.can_read());
    EXPECT_EQ(broadcast.read(), 42);
    tick();
    EXPECT_FALSE(broadcast.can_read());
}

TEST_F(BroadcastTest, CanReadFromLatencyThreeFifoInThreeCycles)
{
    Broadcast<int> broadcast("broadcast", *clock, 3);
    broadcast.put(101);
    EXPECT_FALSE(broadcast.can_read());
    tick();
    EXPECT_FALSE(broadcast.can_read());
    tick();
    EXPECT_FALSE(broadcast.can_read());
    tick();
    ASSERT_TRUE(broadcast.can_read());
    EXPECT_EQ(broadcast.read(), 101);
}

TEST_F(BroadcastTest, CanReadTwiceFromBroadcast)
{
    Broadcast<int> broadcast("broadcast", *clock, 1);
    broadcast.put(42);
    EXPECT_FALSE(broadcast.can_read());
    tick();
    ASSERT_TRUE(broadcast.can_read());
    EXPECT_EQ(broadcast.read(), 42);
    ASSERT_TRUE(broadcast.can_read());
    EXPECT_EQ(broadcast.read(), 42);
}

TEST_F(BroadcastTest, CantReadFromLatencyOneInTwoCycles)
{
    Broadcast<int> broadcast("broadcast", *clock, 1);
    broadcast.put(42);
    tick();
    tick();
    ASSERT_FALSE(broadcast.can_read());
}

} // namespace sc
} // namespace ampere
