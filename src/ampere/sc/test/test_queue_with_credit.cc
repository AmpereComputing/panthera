//
// Copyright (c) 2023, Ampere Computing
// SPDX-License-Identifier: BSD-3-Clause
//
#include "ampere/sc/fifo/queue_with_credit.h"
#include "ampere/sc/testing/test.h"
#include "ampere/sc/testing/assertions.h"

using namespace ampere::sc;
using ::testing::HasSubstr;

/* A `QueueWithCredit` is almost identical to a `Queue` except that it supports the
 * `TakerWithCredit` interface (i.e., `can_take_with_credit()` and `take_with_credit()`).
 *
 * When you use a `QueueWithCredit` kind of FIFO, you're basically giving management of
 * storage to the FIFO while keeping the option of managing credit return yourself. It's
 * a bit of a weird hybrid between `DelayWithCredit` and `Queue`.
 *
 * The upshot of this is that these tests are carbon-copies of the tests for `Queue`
 * except for the one that checks that the credit can be taken and returned explicitly.
 */
class QueueWithCreditTest : public Test
{};

TEST_F(QueueWithCreditTest, CantTakeFromEmptyQueue)
{
    QueueWithCredit<int> queue("queue", *clock, 1, 1, 1);
    start_simulation();
    EXPECT_FALSE(queue.can_take());
}

TEST_F(QueueWithCreditTest, CanPutToEmptyQueue)
{
    QueueWithCredit<int> queue("queue", *clock, 1, 1, 1);
    start_simulation();
    EXPECT_TRUE(queue.can_put());
}

TEST_F(QueueWithCreditTest, CanPutOnlyOneItemInCapacityOneQueue)
{
    QueueWithCredit<int> queue("queue", *clock, 1, 1, 1);
    start_simulation();
    queue.put(86);
    EXPECT_FALSE(queue.can_put());
}

TEST_F(QueueWithCreditTest, CanTakeFromLatencyOneQueueInOneCycle)
{
    QueueWithCredit<int> queue("queue", *clock, 1, 1, 1);
    start_simulation();
    queue.put(2112);
    EXPECT_FALSE(queue.can_take());
    tick();
    ASSERT_TRUE(queue.can_take());
    EXPECT_EQ(queue.take(), 2112);
}

TEST_F(QueueWithCreditTest, CanTakeFromLatencyOneQueueInMoreThanOneCycle)
{
    QueueWithCredit<int> queue("queue", *clock, 1, 1, 1);
    start_simulation();
    queue.put(2112);
    EXPECT_FALSE(queue.can_take());
    tick();
    tick();
    tick();
    ASSERT_TRUE(queue.can_take());
    EXPECT_EQ(queue.take(), 2112);
}

TEST_F(QueueWithCreditTest, CannotOverfillQueue)
{
    QueueWithCredit<int> queue("queue", *clock, 4, 4, 1);
    start_simulation();
    queue.put(10);
    for (int i = 0; i < 4; i++) {
        EXPECT_FALSE(queue.can_put());
        EXPECT_FALSE(queue.can_take());
        tick();
    }
    EXPECT_TRUE(queue.can_take());
    EXPECT_EQ(queue.take(), 10);
}

TEST_F(QueueWithCreditTest, CanPutNItemsInACapacityNQueue)
{
    QueueWithCredit<int> queue("queue", *clock, 1, 1, 4);
    start_simulation();
    ASSERT_TRUE(queue.can_put());
    queue.put(1);
    tick();
    ASSERT_TRUE(queue.can_put());
    queue.put(2);
    tick();
    ASSERT_TRUE(queue.can_put());
    queue.put(3);
    tick();
    ASSERT_TRUE(queue.can_put());
    queue.put(4);
    tick();
    EXPECT_FALSE(queue.can_put());

    for (int i = 1; i <= 4; i++) {
        tick();
        ASSERT_TRUE(queue.can_take());
        EXPECT_EQ(queue.take(), i);
    }
}

TEST_F(QueueWithCreditTest, CanPeekAtItemsInQueue)
{
    QueueWithCredit<int> queue("queue", *clock, 1, 1, 4);
    start_simulation();
    for (int i = 0; i < 4; i++) {
        queue.put(i + 42);
        tick();
        ASSERT_TRUE(queue.can_take());
        EXPECT_EQ(queue.peek(), 42);
    }

    EXPECT_EQ(queue.take(), 42);
    EXPECT_EQ(queue.unsafe_peek(), 43);
}

TEST_F(QueueWithCreditTest, CanPutAgainLatencyCyclesAfterTake)
{
    QueueWithCredit<int> queue("queue", *clock, 1, 1, 1);
    start_simulation();
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

TEST_F(QueueWithCreditTest, PutMoreThanOnceInACycleIsAnError)
{
    QueueWithCredit<int> queue("queue", *clock, 1, 1, 1);
    start_simulation();
    queue.put(86);
    EXPECT_AMPERE_FATAL(queue.put(87), HasSubstr("cannot put to a full fifo"));
}

TEST_F(QueueWithCreditTest, CanPutUncopyableObjectInQueue)
{
    QueueWithCredit<std::unique_ptr<int>> queue("queue", *clock, 1, 1, 1);
    start_simulation();
    std::unique_ptr<int> up{new int{9000}};
    queue.put(std::move(up));
    tick();
    tick();
    tick();
    std::unique_ptr<int> up2 = queue.take();
    EXPECT_EQ(*up2, 9000);
}

TEST_F(QueueWithCreditTest, CanClearQueue)
{
    QueueWithCredit<int> queue("queue", *clock, 2, 2, 1);
    start_simulation();
    queue.put(4297);
    tick();
    queue.clear();
    tick();
    ASSERT_FALSE(queue.can_take());
}

TEST_F(QueueWithCreditTest, QueueClearIgnoresPutsInSameCycle)
{
    QueueWithCredit<int> queue("queue", *clock, 2, 2, 1);
    start_simulation();
    queue.put(5107);
    queue.clear();
    tick();
    tick();
    ASSERT_TRUE(queue.can_take());
    ASSERT_EQ(5107, queue.take());
}

TEST_F(QueueWithCreditTest, CanClearMulitpleInQueue)
{
    QueueWithCredit<int> queue("queue", *clock, 2, 2, 3);
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

TEST_F(QueueWithCreditTest, CanPutCycleAfterQueueClear)
{
    QueueWithCredit<int> queue("queue", *clock, 2, 2, 2);
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

TEST_F(QueueWithCreditTest, CannotPutMultipleAfterQueueClear)
{
    QueueWithCredit<int> queue("queue", *clock, 2, 2, 1);
    start_simulation();
    queue.put(4297);
    queue.clear();
    EXPECT_AMPERE_FATAL(queue.put(4298), HasSubstr("cannot put to a full fifo"));
    tick();
    tick();
    ASSERT_TRUE(queue.can_take());
    ASSERT_EQ(4297, queue.take());
}

TEST_F(QueueWithCreditTest, ClearReturnsCreditsImmediately)
{
    QueueWithCredit<int> queue("queue", *clock, 2, 2, 1);
    start_simulation();
    queue.put(4297);
    tick();
    ASSERT_FALSE(queue.can_put());
    queue.clear();
    ASSERT_TRUE(queue.can_put());
    queue.put(4298);
    tick();
    tick();
    ASSERT_TRUE(queue.can_take());
    ASSERT_EQ(4298, queue.take());
}

TEST_F(QueueWithCreditTest, NonCopyableClearReturnsCreditsImmediately)
{
    int five{5};
    int seven{7};
    QueueWithCredit<std::unique_ptr<int>> queue("queue", *clock, 2, 2, 1);
    start_simulation();
    queue.put(std::make_unique<int>(five));
    ASSERT_FALSE(queue.can_put());
    tick();
    ASSERT_FALSE(queue.can_put());
    queue.clear();
    ASSERT_TRUE(queue.can_put());
    queue.put(std::make_unique<int>(seven));
    tick();
    tick();
    ASSERT_TRUE(queue.can_take());
    ASSERT_EQ(7, *queue.take());
}

struct QueueThingWithComplexClear
{
    int* called_ptr;
    void fifo_clear() { ++*called_ptr; }
    explicit QueueThingWithComplexClear(int* called) : called_ptr(called) {}
};

TEST_F(QueueWithCreditTest, ClearWillInvokeFifoClearMethod)
{
    QueueWithCredit<QueueThingWithComplexClear> queue("queue", *clock, 2, 2, 4);
    int called{0};
    QueueThingWithComplexClear thing1{&called};
    QueueThingWithComplexClear thing2{&called};
    start_simulation();
    queue.put(thing1);
    tick();
    queue.put(thing2);
    tick();
    tick();
    queue.clear();
    EXPECT_EQ(called, 2);
}

TEST_F(QueueWithCreditTest, Size)
{
    QueueWithCredit<int> queue("queue", *clock, 5, 5, 3);
    start_simulation();
    queue.put(2112);
    EXPECT_EQ(queue.size(), 1ul);
    tick();
    queue.put(1234);
    EXPECT_EQ(queue.size(), 2ul);
    tick();
    queue.put(4321);
    EXPECT_EQ(queue.size(), 3ul);
    tick();
    tick();
    tick();
    ASSERT_TRUE(queue.can_take());
    EXPECT_EQ(queue.take(), 2112);
    EXPECT_EQ(queue.size(), 2ul);
}

TEST_F(QueueWithCreditTest, PeekAtAllEntries)
{
    QueueWithCredit<int> queue("queue", *clock, 5, 5, 3);
    start_simulation();
    queue.put(2112);
    tick();
    queue.put(1234);
    tick();
    queue.put(4321);

    ASSERT_FALSE(queue.can_take());
    EXPECT_EQ(queue.unsafe_peek(0), 2112);
    EXPECT_EQ(queue.unsafe_peek(1), 1234);
    EXPECT_EQ(queue.unsafe_peek(2), 4321);

    tick();
    tick();
    tick();

    ASSERT_TRUE(queue.can_take());
    EXPECT_EQ(queue.take(), 2112);
    EXPECT_EQ(queue.unsafe_peek(0), 1234);
    EXPECT_EQ(queue.unsafe_peek(1), 4321);
}

TEST_F(QueueWithCreditTest, AsymmetricCreditReturn)
{
    QueueWithCredit<int> queue55("queue55", *clock, 5, 5, 1);
    QueueWithCredit<int> queue51("queue51", *clock, 5, 1, 1);
    start_simulation();
    queue55.put(2112);
    queue51.put(2112);

    tick();
    ASSERT_FALSE(queue55.can_put());
    ASSERT_FALSE(queue51.can_put());

    tick();
    tick();
    tick();
    tick();

    ASSERT_TRUE(queue55.can_take());
    ASSERT_TRUE(queue51.can_take());
    ASSERT_EQ(queue55.take(), 2112);
    ASSERT_EQ(queue51.take(), 2112);

    tick();

    // The queue with a 1-cycle credit return can put a new entry 1 cycle after the previous one is taken
    ASSERT_FALSE(queue55.can_put());
    ASSERT_TRUE(queue51.can_put());
}

TEST_F(QueueWithCreditTest, TestExplicitCreditManagement)
{
    QueueWithCredit<int> queue("queue", *clock, 1, 1, 2);
    start_simulation();

    queue.put(1);
    tick();
    queue.put(2);
    tick();

    EXPECT_FALSE(queue.can_put());

    auto [x, c1] = queue.take_with_credit();
    tick();
    auto [y, c2] = queue.take_with_credit();

    // Can't put yet: we haven't returned any credits
    EXPECT_FALSE(queue.can_put());

    queue.put_credit(std::move(c1));

    // Still can't put: it takes a cycle for the credit to traverse
    EXPECT_FALSE(queue.can_put());

    tick();

    EXPECT_TRUE(queue.can_put());
    queue.put(3);

    tick();

    EXPECT_FALSE(queue.can_put());

    queue.put_credit(std::move(c2));
    tick();

    EXPECT_TRUE(queue.can_put());
}

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
