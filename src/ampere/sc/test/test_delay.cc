//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
//
// Created by vagrant on 7/30/18.
//
#include "ampere/sc/fifo/delay.h"
#include "ampere/sc/testing/test.h"
#include "ampere/sc/testing/clock_dummy.h"

using namespace ampere::sc;
using ::testing::Return;

template<typename T = int>
struct DelayTestBase : public Test
{
    Delay<T> delay;
    DelayTestBase(size_t bw = 1) : delay("delay", *clock, bw) {}
};

using DelayTest = DelayTestBase<>;

TEST_F(DelayTest, CantReadFromEmptyFifo)
{
    EXPECT_FALSE(delay.can_take());
}

TEST_F(DelayTest, CanWriteToEmptyFifo)
{
    EXPECT_TRUE(delay.can_put());
    delay.put(42);
}

TEST_F(DelayTest, CantWriteToFullFifo)
{
    delay.put(2001);
    EXPECT_FALSE(delay.can_put());
}

TEST_F(DelayTest, PuttingToAFullFifoIsAnError)
{
    delay.put(2001);
    EXPECT_ANY_THROW(delay.put(2002));
}

struct DelayTwoBandwidthTest : DelayTestBase<>
{
    DelayTwoBandwidthTest() : DelayTestBase<>(2) {}
};

TEST_F(DelayTwoBandwidthTest, CanWriteWhileEntryInDelay)
{
    delay.put(2029);
    tick();
    EXPECT_TRUE(delay.can_put());
    delay.put(2030);
    tick();
    EXPECT_TRUE(delay.can_take());
    EXPECT_EQ(2029, delay.take());
    tick();
    EXPECT_TRUE(delay.can_take());
    EXPECT_EQ(2030, delay.take());
}

TEST_F(DelayTest, CanReadFromLatencyOneFifoInOneCycle)
{
    delay.put(42);
    EXPECT_FALSE(delay.can_take());
    tick();
    ASSERT_TRUE(delay.can_take());
    EXPECT_EQ(delay.take(), 42);
}

struct DelayThreeBandwidthTest : DelayTestBase<>
{
    DelayThreeBandwidthTest() : DelayTestBase<>(3) {}
};

TEST_F(DelayThreeBandwidthTest, CanReadFromLatencyThreeFifoInThreeCycles)
{
    delay.put(101);
    EXPECT_FALSE(delay.can_take());
    tick();
    EXPECT_FALSE(delay.can_take());
    tick();
    EXPECT_FALSE(delay.can_take());
    tick();
    ASSERT_TRUE(delay.can_take());
    EXPECT_EQ(delay.take(), 101);
}

TEST_F(DelayTest, CantTakeTwiceFromFifo)
{
    delay.put(12);
    tick();
    ASSERT_TRUE(delay.can_take());
    EXPECT_EQ(delay.take(), 12);
    EXPECT_FALSE(delay.can_take());
}

TEST_F(DelayTest, CanPeekWithoutTaking)
{
    delay.put(12);
    tick();
    ASSERT_TRUE(delay.can_take());
    EXPECT_EQ(delay.peek(), 12);
    ASSERT_TRUE(delay.can_take());
    EXPECT_EQ(delay.take(), 12);
    EXPECT_FALSE(delay.can_take());
}

TEST_F(DelayTest, FailureToReadIsAnError)
{
    delay.put(1999);
    tick();
    tick();
    EXPECT_THROW(delay.can_take(), std::runtime_error);
}

TEST_F(DelayTest, FailureToReadIsAnErrorDetectedAtPut)
{
    delay.put(1999);
    tick();
    tick();
    EXPECT_THROW(delay.can_put(), std::runtime_error);
}

using DelayUncopyableTest = DelayTestBase<std::unique_ptr<int>>;

TEST_F(DelayUncopyableTest, CanPutUncopyableObjectInDelay)
{
    std::unique_ptr<int> up{new int{42}};
    delay.put(std::move(up));
    EXPECT_EQ(up, nullptr);
    tick();
    ASSERT_TRUE(delay.can_take());
    std::unique_ptr<int> up2 = delay.take();
    EXPECT_EQ(*up2, 42);
}

TEST_F(DelayTwoBandwidthTest, CanClearDelay)
{
    delay.put(4297);
    tick();
    delay.clear();
    tick();
    ASSERT_FALSE(delay.can_take());
}

TEST_F(DelayTwoBandwidthTest, CanPutCycleAfterDelayClear)
{
    delay.put(5209);
    tick();
    delay.clear();
    delay.put(5210);
    tick();
    ASSERT_FALSE(delay.can_take());
    delay.put(5211);
    delay.clear();
    tick();
    ASSERT_FALSE(delay.can_take());
    tick();
    ASSERT_TRUE(delay.can_take());
    ASSERT_EQ(5211, delay.take());
}

TEST_F(DelayTwoBandwidthTest, CannotPutMultipleAfterDelayClear)
{
    delay.put(4297);
    delay.clear();
    ASSERT_THROW(delay.put(4298), std::runtime_error);
    tick();
    tick();
    ASSERT_TRUE(delay.can_take());
    ASSERT_EQ(4297, delay.take());
}

TEST_F(DelayTwoBandwidthTest, DelayClearIgnoresPutsInSameCycle)
{
    delay.put(5107);
    delay.clear();
    tick();
    tick();
    ASSERT_TRUE(delay.can_take());
    ASSERT_EQ(5107, delay.take());
}

TEST_F(DelayTwoBandwidthTest, DelayCantTakeAfterClear)
{
    delay.put(5113);
    tick();
    tick();
    delay.clear();
    ASSERT_FALSE(delay.can_take());
}

TEST_F(DelayTwoBandwidthTest, DelayCanTakeBeforeClear)
{
    delay.put(5113);
    tick();
    delay.put(5114);
    tick();
    ASSERT_EQ(5113, delay.take());
    delay.clear();
    ASSERT_FALSE(delay.can_take());
    tick();
    ASSERT_FALSE(delay.can_take());
}

struct ThingWithComplexClear
{
    bool *called_ptr;
    void fifo_clear() { *called_ptr = true; }
    explicit ThingWithComplexClear(bool *called) : called_ptr(called) {}
};

struct DelayComplexClearTest : DelayTestBase<ThingWithComplexClear>
{
    DelayComplexClearTest() : DelayTestBase<ThingWithComplexClear>(2) {}
};

TEST_F(DelayComplexClearTest, ClearWillInvokeFifoClearMethod)
{
    bool called{false};
    ThingWithComplexClear thing{&called};
    delay.put(thing);
    tick();
    delay.clear();
    EXPECT_TRUE(called);
}

struct CounterWithClear
{
    int *called_ptr;
    void fifo_clear() { *called_ptr = *called_ptr + 1; }
    explicit CounterWithClear(int *called) : called_ptr(called) {}
};

using DelayClearTest = DelayTestBase<CounterWithClear>;

TEST_F(DelayClearTest, ClearWillInvokeFifoClearMethodWithCounter2Things)
{
    int called{0};
    CounterWithClear one{&called};
    CounterWithClear two{&called};
    delay.put(one);
    tick();
    delay.take();
    delay.put(two);
    tick();
    delay.clear();
    EXPECT_EQ(1, called);
}

struct DelayClearMoreComplexTest : public Test
{
    int called{0};
};

TEST_F(DelayClearMoreComplexTest, ClearWillInvokeFifoClearMethodWithCounter)
{
    Delay<CounterWithClear> delay("delay", *clock, 2);
    int called{0};
    CounterWithClear thing{&called};
    delay.put(thing);
    tick();
    delay.clear();
    EXPECT_EQ(1, called);
}

TEST_F(DelayClearMoreComplexTest, ClearWillInvokeFifoClearMethodWithUniquePtr)
{
    Delay<std::unique_ptr<CounterWithClear>> delay("delay", *clock, 2);
    int called{0};
    auto thing = std::make_unique<CounterWithClear>(&called);
    delay.put(std::move(thing));
    tick();
    delay.clear();
    EXPECT_EQ(1, called);
}

TEST_F(DelayClearMoreComplexTest, ClearWillInvokeFifoClearMethodWith2UniquePtr)
{
    Delay<std::unique_ptr<CounterWithClear>> delay("delay", *clock, 1);
    int called{0};
    auto one = std::make_unique<CounterWithClear>(&called);
    auto two = std::make_unique<CounterWithClear>(&called);
    delay.put(std::move(one));
    tick();
    delay.take();
    delay.put(std::move(two));
    tick();
    delay.clear();
    EXPECT_EQ(1, called);
}

TEST_F(DelayThreeBandwidthTest, Size)
{
    delay.put(2001);
    EXPECT_EQ(delay.size(), 1ul);
    tick();
    delay.put(2002);
    EXPECT_EQ(delay.size(), 2ul);
    tick();
    EXPECT_EQ(delay.size(), 2ul);
    tick();
    ASSERT_TRUE(delay.can_take());
    EXPECT_EQ(delay.take(), 2001);
    EXPECT_EQ(delay.size(), 1ul);
}

TEST_F(DelayThreeBandwidthTest, Ready)
{
    delay.put(2001);
    tick();
    delay.put(2002);
    tick();

    EXPECT_EQ(delay.num_ready(), 0);
    tick();
    EXPECT_EQ(delay.num_ready(), 1);
    delay.take();

    tick();
    EXPECT_EQ(delay.num_ready(), 1);
}

TEST_F(DelayThreeBandwidthTest, PeekAtAllEntries)
{
    delay.put(2001);
    tick();
    delay.put(2002);
    tick();

    EXPECT_EQ(delay.unsafe_peek(0), 2001);
    EXPECT_EQ(delay.unsafe_peek(1), 2002);

    tick();
    ASSERT_TRUE(delay.can_take());
    EXPECT_EQ(delay.take(), 2001);
    EXPECT_EQ(delay.unsafe_peek(0), 2002);
}

struct RunnableDummy : public ampere::sc::Runnable
{
    bool run() override { return false; }
};

class DelayTestMockClock : public Test, public ClockMock
{};

TEST_F(DelayTestMockClock, LatencyOneFifoSetsTriggerWithOneCycleDelay)
{
    Delay<int> delay("delay", *this, 1);
    RunnableDummy runnable;
    delay.add_target(&runnable);
    EXPECT_CALL(*this, schedule(&runnable, 1));
    delay.put(2112);
}

TEST_F(DelayTestMockClock, LatencyTwoFifoSetsTriggerWithTwoCycleDelay)
{
    Delay<int> delay("delay", *this, 2);
    RunnableDummy runnable;
    delay.add_target(&runnable);
    EXPECT_CALL(*this, schedule(&runnable, 2));
    delay.put(2112);
}

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
