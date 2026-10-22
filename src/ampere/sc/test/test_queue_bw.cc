//
// Copyright (c) 2018-2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include "ampere/sc/fifo/queue_bw.h"
#include "ampere/sc/testing/test.h"

using namespace ampere::sc;
using ::testing::Return;

template<typename T = int>
struct QueueBWTestBase : public Test
{
    QueueBW<T> queue;
    QueueBWTestBase(CycleDelta latency = 1,
                    CycleDelta return_latency = 1,
                    size_t capacity = 2,
                    size_t rd_bandwidth = 1,
                    size_t wr_bandwidth = 1,
                    bool lossy = false)
        : queue("queue", *clock, latency, return_latency, capacity, rd_bandwidth, wr_bandwidth, lossy)
    {
    }

    void SetUp() override { start_simulation(); }

    void put_multiple(int amount, int start = 0)
    {
        for (int i = 1; i <= amount; ++i)
            queue.put(start + i);
    }
};

class QueueBWTest : public Test
{};

struct QueueBWMediumMoreWrThanRdTest : QueueBWTestBase<>
{
    QueueBWMediumMoreWrThanRdTest() : QueueBWTestBase<>(1, 1, 2, 1, 2) {}
};

TEST_F(QueueBWMediumMoreWrThanRdTest, CanPutTwiceToWrBandwidthTwoQueue)
{
    queue.put(1);
    EXPECT_TRUE(queue.can_put());
    EXPECT_NO_THROW(queue.put(2));
}

struct QueueBWSmallMoreWrThanRdTest : QueueBWTestBase<>
{
    QueueBWSmallMoreWrThanRdTest() : QueueBWTestBase<>(1, 1, 1, 1, 2) {}
};

TEST_F(QueueBWSmallMoreWrThanRdTest, CantPutTwiceToWrBandwidthTwoQueueIfCapacityExceeded)
{
    queue.put(1);
    EXPECT_FALSE(queue.can_put());
    EXPECT_ANY_THROW(queue.put(2));
}

struct QueueBWEqualRdWrTest : QueueBWTestBase<>
{
    QueueBWEqualRdWrTest() : QueueBWTestBase<>(1, 1, 2, 2, 2) {}
};

TEST_F(QueueBWEqualRdWrTest, CanTakeTwiceFromRdBandwidthTwoQueue)
{
    queue.put(1);
    queue.put(2);
    EXPECT_FALSE(queue.can_take());

    tick();

    EXPECT_TRUE(queue.can_take());
    EXPECT_EQ(queue.take(), 1);
    EXPECT_TRUE(queue.can_take());
    EXPECT_EQ(queue.take(), 2);
}

struct QueueBWLargeMoreWrThanRdTest : QueueBWTestBase<>
{
    QueueBWLargeMoreWrThanRdTest() : QueueBWTestBase<>(1, 1, 8, 2, 4) {}
};

TEST_F(QueueBWLargeMoreWrThanRdTest, ReadAndWriteBandwidthsMayBeDifferent)
{
    put_multiple(4);

    EXPECT_FALSE(queue.can_put());
    EXPECT_FALSE(queue.can_take());

    tick();

    EXPECT_EQ(queue.take(), 1);
    EXPECT_EQ(queue.take(), 2);
    EXPECT_FALSE(queue.can_take());

    tick();

    EXPECT_EQ(queue.take(), 3);
    EXPECT_EQ(queue.take(), 4);
    EXPECT_FALSE(queue.can_take());
}

TEST_F(QueueBWLargeMoreWrThanRdTest, PutAndGetInSameCycle)
{
    put_multiple(4);
    EXPECT_EQ(queue.size(), 4);
    for (unsigned i = 0; i < 4; ++i)
        EXPECT_EQ(queue.get(i), i + 1);
}

struct QueueBWMovableTest : QueueBWTestBase<std::unique_ptr<int>>
{
    QueueBWMovableTest() : QueueBWTestBase<std::unique_ptr<int>>(1, 1, 2, 2, 2) {}
};

TEST_F(QueueBWMovableTest, CanPutUncopyableObjectInQueueBW)
{
    std::unique_ptr<int> up1{new int{201}};
    std::unique_ptr<int> up2{new int{202}};
    queue.put(std::move(up1));
    queue.put(std::move(up2));

    tick(3);

    std::unique_ptr<int> up3 = queue.take();
    std::unique_ptr<int> up4 = queue.take();
    EXPECT_EQ(*up3, 201);
    EXPECT_EQ(*up4, 202);
}

TEST_F(QueueBWEqualRdWrTest, CanClearBWQueue)
{
    queue.put(4297);
    tick();
    queue.clear();
    tick();
    ASSERT_FALSE(queue.can_take());
}

TEST_F(QueueBWEqualRdWrTest, CanPut2ItemsAndClear1)
{
    put_multiple(2);
    ASSERT_FALSE(queue.can_put());
    tick();

    ASSERT_FALSE(queue.can_put());
    ASSERT_TRUE(queue.can_take());
    ASSERT_EQ(1, queue.take());
    ASSERT_TRUE(queue.can_take());

    queue.clear();

    ASSERT_TRUE(queue.can_put());
    ASSERT_FALSE(queue.can_take());

    tick();

    ASSERT_FALSE(queue.can_take());
}

TEST_F(QueueBWMediumMoreWrThanRdTest, Size)
{
    queue.put(1);
    EXPECT_EQ(queue.size(), 1ul);
    queue.put(2);
    EXPECT_EQ(queue.size(), 2ul);
    ASSERT_FALSE(queue.can_take());

    tick();

    ASSERT_TRUE(queue.can_take());
    EXPECT_EQ(queue.take(), 1);
    EXPECT_EQ(queue.size(), 1ul);
}

TEST_F(QueueBWMediumMoreWrThanRdTest, Ready)
{
    queue.put(1);
    queue.put(2);
    tick();

    ASSERT_EQ(queue.num_ready(), 2);
}

TEST_F(QueueBWMediumMoreWrThanRdTest, PeekAtAllEntries)
{
    queue.put(1);
    queue.put(2);
    EXPECT_EQ(queue.unsafe_peek(0), 1);
    EXPECT_EQ(queue.unsafe_peek(1), 2);
    ASSERT_FALSE(queue.can_take());

    tick();

    ASSERT_TRUE(queue.can_take());
    EXPECT_EQ(queue.take(), 1);
    EXPECT_EQ(queue.unsafe_peek(0), 2);
}

TEST_F(QueueBWTest, AsymmetricCreditReturn)
{
    QueueBW<int> queue33("queue33", *clock, 3, 3, 2, 1, 2);
    QueueBW<int> queue31("queue31", *clock, 3, 1, 2, 1, 2);
    start_simulation();
    queue33.put(1);
    queue33.put(2);
    queue31.put(1);
    queue31.put(2);

    ASSERT_FALSE(queue33.can_put());
    ASSERT_FALSE(queue31.can_put());

    tick();

    ASSERT_FALSE(queue33.can_put());
    ASSERT_FALSE(queue31.can_put());
    ASSERT_FALSE(queue33.can_take());
    ASSERT_FALSE(queue31.can_take());

    tick();
    tick();

    ASSERT_FALSE(queue33.can_put());
    ASSERT_FALSE(queue31.can_put());
    ASSERT_TRUE(queue33.can_take());
    ASSERT_TRUE(queue31.can_take());

    ASSERT_EQ(queue33.take(), 1);
    ASSERT_EQ(queue31.take(), 1);
    ASSERT_FALSE(queue33.can_put());
    ASSERT_FALSE(queue31.can_put());

    tick();

    // The queue with a 1-cycle credit return can put a new entry 1 cycle after the previous one is taken
    ASSERT_FALSE(queue33.can_put());
    ASSERT_TRUE(queue31.can_put());
}

struct QueueBWLossyTest : QueueBWTestBase<>
{
    QueueBWLossyTest() : QueueBWTestBase<>(2, 0, 4, 2, 2, true) {}
};

TEST_F(QueueBWLossyTest, CannotOverwriteInSameCycle)
{
    put_multiple(2);
    EXPECT_FALSE(queue.can_put());
}

TEST_F(QueueBWLossyTest, CanOverwriteInTakeCycle)
{
    put_multiple(2);
    tick();

    put_multiple(2, 2);
    tick();

    EXPECT_TRUE(queue.can_put());
    queue.put(5);

    EXPECT_TRUE(queue.can_put());
    EXPECT_TRUE(queue.can_take());

    queue.put(6);
    EXPECT_FALSE(queue.can_put());
    EXPECT_FALSE(queue.can_take());

    tick();

    EXPECT_TRUE(queue.can_take());
    EXPECT_EQ(queue.take(), 3);
    EXPECT_TRUE(queue.can_take());
    EXPECT_EQ(queue.take(), 4);
}

TEST_F(QueueBWLossyTest, CanOverwriteAfterTakeCycle)
{
    put_multiple(2);
    tick();

    put_multiple(2, 2);
    tick(4);

    EXPECT_TRUE(queue.can_put());
    queue.put(5);

    EXPECT_TRUE(queue.can_put());
    EXPECT_TRUE(queue.can_take());

    queue.put(6);
    EXPECT_FALSE(queue.can_put());
    EXPECT_TRUE(queue.can_take());

    tick();

    EXPECT_TRUE(queue.can_take());
    EXPECT_EQ(queue.take(), 3);
    EXPECT_TRUE(queue.can_take());
    EXPECT_EQ(queue.take(), 4);
}

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
