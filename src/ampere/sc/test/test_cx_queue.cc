//
// Copyright (c) 2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include "ampere/sc/fifo/cx_queue.h"
#include "ampere/sc/testing/test.h"
#include "ampere/sc/testing/clock_dummy.h"

using namespace ampere::sc;

template<typename T = int>
struct CxQueueTestBase : public Test
{
    ClockDummy src_clock{"src_clock"};
    ClockDummy tgt_clock{"tgt_clock"};
    CxQueue<int> cx_queue;
    CxQueueTestBase(size_t capacity = 1) : cx_queue("cx_queue", src_clock, tgt_clock, 0_ns, 0_ns, capacity) {}
};

using CxQueueTest = CxQueueTestBase<>;

TEST_F(CxQueueTest, CantTakeFromEmptyCxQueue)
{
    EXPECT_FALSE(cx_queue.can_take());
}

TEST_F(CxQueueTest, CanPutToEmptyCxQueue)
{
    EXPECT_TRUE(cx_queue.can_put());
}

TEST_F(CxQueueTest, CanPutOnlyOneItemInCapacityCxOneQueue)
{
    cx_queue.put(86);
    EXPECT_FALSE(cx_queue.can_put());
}

struct CxQueueMultipleTest : CxQueueTestBase<>
{
    CxQueueMultipleTest() : CxQueueTestBase<>(3) {}
};

TEST_F(CxQueueMultipleTest, CantPutTwiceInTheSameCycle)
{
    cx_queue.put(86);
    EXPECT_FALSE(cx_queue.can_put());
}

TEST_F(CxQueueMultipleTest, CanPutNItemsInACapacityNCxQueue)
{
    cx_queue.put(1);
    src_clock.tick();
    cx_queue.put(2);
    src_clock.tick();
    cx_queue.put(3);
    src_clock.tick();
    EXPECT_FALSE(cx_queue.can_put());
}

TEST_F(CxQueueMultipleTest, CanTakeNItemsFromACapacityNCxQueue)
{
    cx_queue.put(1);
    src_clock.tick();
    cx_queue.put(2);
    src_clock.tick();
    cx_queue.put(3);
    src_clock.tick();
    EXPECT_FALSE(cx_queue.can_put());

    ASSERT_TRUE(cx_queue.can_take());
    EXPECT_EQ(cx_queue.take(), 1);
    tgt_clock.tick();
    EXPECT_EQ(cx_queue.take(), 2);
    tgt_clock.tick();
    EXPECT_EQ(cx_queue.take(), 3);
    EXPECT_FALSE(cx_queue.can_take());
}

TEST_F(CxQueueMultipleTest, CantTakeTwiceInTheSameCycle)
{
    cx_queue.put(1);
    src_clock.tick();
    cx_queue.put(2);
    src_clock.tick();
    cx_queue.put(3);
    src_clock.tick();
    EXPECT_FALSE(cx_queue.can_put());

    EXPECT_EQ(cx_queue.take(), 1);
    EXPECT_FALSE(cx_queue.can_take());
}

TEST_F(CxQueueMultipleTest, Size)
{
    cx_queue.put(1);
    EXPECT_EQ(cx_queue.size(), 1ul);
    src_clock.tick();
    cx_queue.put(2);
    src_clock.tick();
    EXPECT_EQ(cx_queue.size(), 2ul);
    cx_queue.put(3);
    src_clock.tick();
    EXPECT_EQ(cx_queue.size(), 3ul);

    ASSERT_TRUE(cx_queue.can_take());
    EXPECT_EQ(cx_queue.take(), 1);
    EXPECT_EQ(cx_queue.size(), 2ul);
}

TEST_F(CxQueueMultipleTest, Ready)
{
    cx_queue.put(1);
    EXPECT_EQ(cx_queue.num_ready(), 1);
    src_clock.tick();

    cx_queue.put(2);
    EXPECT_EQ(cx_queue.num_ready(), 2);
    src_clock.tick();

    cx_queue.put(3);
    EXPECT_EQ(cx_queue.num_ready(), 3);
}

TEST_F(CxQueueMultipleTest, PeekAtAllEntries)
{
    cx_queue.put(1);
    src_clock.tick();
    cx_queue.put(2);
    src_clock.tick();
    cx_queue.put(3);
    src_clock.tick();

    EXPECT_EQ(cx_queue.unsafe_peek(0), 1);
    EXPECT_EQ(cx_queue.unsafe_peek(1), 2);
    EXPECT_EQ(cx_queue.unsafe_peek(2), 3);

    ASSERT_TRUE(cx_queue.can_take());
    EXPECT_EQ(cx_queue.take(), 1);
    EXPECT_EQ(cx_queue.unsafe_peek(0), 2);
    EXPECT_EQ(cx_queue.unsafe_peek(1), 3);
}

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
