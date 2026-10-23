//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
//
// Copyright (c) 2018, Ampere Computing
//
#include "ampere/sc/fifo/queue.h"
#include "ampere/sc/testing/test.h"
#include <gmock/gmock.h>

using namespace ampere::sc;

template<typename T = int>
struct QueueTestBase : public Test
{
    Queue<T> queue;
    QueueTestBase(CycleDelta latency = 1, CycleDelta return_latency = 1, size_t capacity = 1, bool lossy = false)
        : queue("queue", *clock, latency, return_latency, capacity, lossy)
    {
    }

    void SetUp() override { start_simulation(); }

    void put_each_tick(int amount)
    {
        for (int i = 1; i <= amount; ++i) {
            ASSERT_TRUE(queue.can_put());
            queue.put(i);
            tick();
        }
    }

    void take_each_tick(int amount)
    {
        for (int i = 1; i <= amount; ++i) {
            ASSERT_TRUE(queue.can_take());
            queue.take();
            tick();
        }
    }
};

struct QueueTest : Test
{};
using QueueSingleLatencyCapacityTest = QueueTestBase<>;

TEST_F(QueueSingleLatencyCapacityTest, CantTakeFromEmptyQueue)
{
    EXPECT_FALSE(queue.can_take());
}

TEST_F(QueueSingleLatencyCapacityTest, CanPutToEmptyQueue)
{
    EXPECT_TRUE(queue.can_put());
}

TEST_F(QueueSingleLatencyCapacityTest, CanPutOnlyOneItem)
{
    queue.put(86);
    EXPECT_FALSE(queue.can_put());
}

TEST_F(QueueSingleLatencyCapacityTest, CanTakeFromInOneCycle)
{
    queue.put(2112);
    EXPECT_FALSE(queue.can_take());
    tick();
    ASSERT_TRUE(queue.can_take());
    EXPECT_EQ(queue.take(), 2112);
}

TEST_F(QueueSingleLatencyCapacityTest, CanTakeFromInMoreThanOneCycle)
{
    queue.put(2112);
    EXPECT_FALSE(queue.can_take());
    tick(3);
    ASSERT_TRUE(queue.can_take());
    EXPECT_EQ(queue.take(), 2112);
}

struct QueueLatencyLargerThanCapacityTest : QueueTestBase<>
{
    QueueLatencyLargerThanCapacityTest() : QueueTestBase<>(4, 4, 1) {}
};

TEST_F(QueueLatencyLargerThanCapacityTest, CannotOverfillQueue)
{
    queue.put(10);
    for (int i = 0; i < 4; i++) {
        EXPECT_FALSE(queue.can_put());
        EXPECT_FALSE(queue.can_take());
        tick();
    }
    EXPECT_TRUE(queue.can_take());
    EXPECT_EQ(queue.take(), 10);
}

struct QueueCapacityLargerThanLatencyTest : QueueTestBase<>
{
    QueueCapacityLargerThanLatencyTest() : QueueTestBase<>(1, 1, 4) {}
};

TEST_F(QueueCapacityLargerThanLatencyTest, CanPutNItemsInACapacityNQueue)
{
    put_each_tick(4);
    EXPECT_FALSE(queue.can_put());

    for (int i = 1; i <= 4; i++) {
        tick();
        ASSERT_TRUE(queue.can_take());
        EXPECT_EQ(queue.take(), i);
    }
}

TEST_F(QueueCapacityLargerThanLatencyTest, CanPutAndGet)
{
    EXPECT_EQ(queue.size(), 0);

    queue.put(5);
    EXPECT_EQ(queue.size(), 1);
    EXPECT_EQ(queue.get(), 5);
}

TEST_F(QueueCapacityLargerThanLatencyTest, CanPeekAtItemsInQueue)
{
    for (int i = 0; i < 4; i++) {
        queue.put(i + 42);
        tick();
        ASSERT_TRUE(queue.can_take());
        EXPECT_EQ(queue.peek(), 42);
    }

    EXPECT_EQ(queue.take(), 42);
    EXPECT_EQ(queue.peek(), 43);
}

TEST_F(QueueSingleLatencyCapacityTest, CanPutAgainLatencyCyclesAfterTake)
{
    queue.put(99);
    tick();
    EXPECT_FALSE(queue.can_put());
    EXPECT_TRUE(queue.can_take());
    tick();
    EXPECT_FALSE(queue.can_put());
    EXPECT_TRUE(queue.can_take());
    tick();
    EXPECT_EQ(queue.take(), 99);
    EXPECT_FALSE(queue.can_put());
    tick();
    EXPECT_TRUE(queue.can_put());
}

TEST_F(QueueSingleLatencyCapacityTest, PutMoreThanOnceInACycleIsAnError)
{
    queue.put(86);
    EXPECT_ANY_THROW(queue.put(87));
}

using QueueMovableTest = QueueTestBase<std::unique_ptr<int>>;

TEST_F(QueueMovableTest, CanPutUncopyableObjectInQueue)
{
    std::unique_ptr<int> up{new int{9000}};
    queue.put(std::move(up));
    tick(3);
    std::unique_ptr<int> up2 = queue.take();
    EXPECT_EQ(*up2, 9000);
}

struct QueueMediumLatencyTest : QueueTestBase<>
{
    QueueMediumLatencyTest() : QueueTestBase<>(2, 2, 1) {}
};

TEST_F(QueueMediumLatencyTest, CanClearQueue)
{
    queue.put(4297);
    tick();
    queue.clear();
    tick();
    ASSERT_FALSE(queue.can_take());
}

TEST_F(QueueMediumLatencyTest, QueueClearIgnoresPutsInSameCycle)
{
    queue.put(5107);
    queue.clear();
    tick();
    tick();
    ASSERT_TRUE(queue.can_take());
    ASSERT_EQ(5107, queue.take());
}

TEST_F(QueueTest, CanClearMultipleInQueue)
{
    Queue<int> queue("queue", *clock, 2, 2, 3);
    start_simulation();
    queue.put(4297);
    tick();
    queue.put(4298);
    tick();
    queue.put(4299);
    tick();
    queue.clear();
    tick();
    ASSERT_FALSE(queue.can_take());
    tick();
    ASSERT_FALSE(queue.can_take());
    tick();
    ASSERT_FALSE(queue.can_take());
}

TEST_F(QueueTest, CanPutCycleAfterQueueClear)
{
    Queue<int> queue("queue", *clock, 2, 2, 2);
    start_simulation();
    queue.put(5209);
    tick();
    queue.clear();
    ASSERT_TRUE(queue.can_put());
    queue.put(5210);
    tick();
    ASSERT_TRUE(queue.can_put());
    queue.put(5211);
    ASSERT_FALSE(queue.can_take());
    tick();
    ASSERT_TRUE(queue.can_take());
    ASSERT_EQ(5210, queue.take());
    ASSERT_FALSE(queue.can_take());
    queue.clear();
    ASSERT_TRUE(queue.can_put());
    tick();
    ASSERT_FALSE(queue.can_take());
    tick();
    ASSERT_FALSE(queue.can_take());
}

TEST_F(QueueMediumLatencyTest, CannotPutMultipleAfterQueueClear)
{
    queue.put(4297);
    queue.clear();
    ASSERT_THROW(queue.put(4298), std::runtime_error);
    tick(2);
    ASSERT_TRUE(queue.can_take());
    ASSERT_EQ(4297, queue.take());
}

TEST_F(QueueMediumLatencyTest, ClearReturnsCreditsImmediately)
{
    queue.put(4297);
    tick();
    ASSERT_FALSE(queue.can_put());
    queue.clear();
    ASSERT_TRUE(queue.can_put());
    queue.put(4298);
    tick(2);
    ASSERT_TRUE(queue.can_take());
    ASSERT_EQ(4298, queue.take());
}

TEST_F(QueueMovableTest, NonCopyableClearReturnsCreditsImmediately)
{
    int five{5};
    int seven{7};
    queue.put(std::make_unique<int>(five));
    ASSERT_FALSE(queue.can_put());
    tick();
    ASSERT_FALSE(queue.can_put());
    queue.clear();
    ASSERT_TRUE(queue.can_put());
    queue.put(std::make_unique<int>(seven));
    tick(2);
    ASSERT_TRUE(queue.can_take());
    ASSERT_EQ(7, *queue.take());
}

struct QueueThingWithComplexClear
{
    int* called_ptr;
    void fifo_clear() { ++*called_ptr; }
    explicit QueueThingWithComplexClear(int* called) : called_ptr(called) {}
};

struct FifoClearQueueTest : QueueTestBase<QueueThingWithComplexClear>
{
    int called{0};
    QueueThingWithComplexClear thing1{&called};
    QueueThingWithComplexClear thing2{&called};
    FifoClearQueueTest() : QueueTestBase<QueueThingWithComplexClear>(2, 2, 4) {}
};

TEST_F(FifoClearQueueTest, ClearWillInvokeFifoClearMethod)
{
    queue.put(thing1);
    tick();
    queue.put(thing2);
    tick(2);
    queue.clear();
    EXPECT_EQ(called, 2);
}

struct QueueMidTest : QueueTestBase<>
{
    QueueMidTest() : QueueTestBase<>(5, 5, 3) {}

    void SetUp() override
    {
        QueueTestBase<>::SetUp();

        queue.put(2112);
        EXPECT_EQ(queue.size(), 1ul);
        tick();
        queue.put(1234);
        EXPECT_EQ(queue.size(), 2ul);
        tick();
        queue.put(4321);
        EXPECT_EQ(queue.size(), 3ul);
    }
};

TEST_F(QueueMidTest, Size)
{
    tick(3);
    ASSERT_TRUE(queue.can_take());
    EXPECT_EQ(queue.take(), 2112);
    EXPECT_EQ(queue.size(), 2ul);
}

TEST_F(QueueMidTest, Ready)
{
    tick(3);
    EXPECT_EQ(queue.num_ready(), 1);
    tick();
    EXPECT_EQ(queue.num_ready(), 2);
    tick();
    EXPECT_EQ(queue.num_ready(), 3);
}

TEST_F(QueueMidTest, PeekAtAllEntries)
{
    ASSERT_FALSE(queue.can_take());
    EXPECT_EQ(queue.unsafe_peek(0), 2112);
    EXPECT_EQ(queue.unsafe_peek(1), 1234);
    EXPECT_EQ(queue.unsafe_peek(2), 4321);

    tick(3);

    ASSERT_TRUE(queue.can_take());
    EXPECT_EQ(queue.take(), 2112);
    EXPECT_EQ(queue.unsafe_peek(0), 1234);
    EXPECT_EQ(queue.unsafe_peek(1), 4321);
}

TEST_F(QueueMidTest, ClearIfSome)
{
    tick();
    queue.clear_if([](auto& e) { return e == 2112 || e == 4321; });
    tick(3);
    EXPECT_TRUE(queue.can_take());
    EXPECT_EQ(queue.take(), 1234);
    tick();
    EXPECT_FALSE(queue.can_take());
}

TEST_F(QueueMidTest, ClearIfIgnoresInSameCycle)
{
    queue.clear_if([](auto& e) { return e == 1234 || e == 4321; });
    tick(3);
    queue.take();
    tick(2);
    EXPECT_TRUE(queue.can_take());
    queue.take();
    tick();
    EXPECT_FALSE(queue.can_take());
}

TEST_F(QueueTest, AsymmetricCreditReturn)
{
    Queue<int> queue55("queue55", *clock, 5, 5, 1);
    Queue<int> queue51("queue51", *clock, 5, 1, 1);
    start_simulation();
    queue55.put(2112);
    queue51.put(2112);

    tick();
    ASSERT_FALSE(queue55.can_put());
    ASSERT_FALSE(queue51.can_put());

    tick(4);

    ASSERT_TRUE(queue55.can_take());
    ASSERT_TRUE(queue51.can_take());
    ASSERT_EQ(queue55.take(), 2112);
    ASSERT_EQ(queue51.take(), 2112);

    tick();

    // The queue with a 1-cycle credit return can put a new entry 1 cycle after the previous one is taken
    ASSERT_FALSE(queue55.can_put());
    ASSERT_TRUE(queue51.can_put());
}

struct QueueLossyTest : QueueTestBase<>
{
    QueueLossyTest() : QueueTestBase<>(4, 0, 4, true) {}
};

TEST_F(QueueLossyTest, CannotOverwriteInSameCycle)
{
    put_each_tick(3);

    queue.put(4);
    EXPECT_FALSE(queue.can_put());
}

TEST_F(QueueLossyTest, CanOverwriteInNextCycle)
{
    put_each_tick(3);
    queue.put(4);
    tick();

    EXPECT_TRUE(queue.can_put());
    queue.put(5);
    EXPECT_FALSE(queue.can_put());
    EXPECT_FALSE(queue.can_take());

    tick();

    EXPECT_TRUE(queue.can_take());
    EXPECT_EQ(queue.take(), 2);
}

struct QueueLossyNonZeroReturnLatencyTest : QueueTestBase<>
{
    QueueLossyNonZeroReturnLatencyTest() : QueueTestBase<>(4, 8, 4, true) {}
};

TEST_F(QueueLossyNonZeroReturnLatencyTest, CannotOverwriteIfReturning)
{
    put_each_tick(4);
    take_each_tick(4);

    EXPECT_FALSE(queue.can_put());
}

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
