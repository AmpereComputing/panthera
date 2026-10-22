//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/sc/fifo/zqueue.h"
#include "ampere/sc/connect.h"
#include "ampere/sc/module.h"
#include "ampere/sc/testing/test.h"

using namespace ampere::sc;
using ::testing::MatchesRegex;

namespace {

template<typename T = int>
struct ZQueueTestBase : public Test
{
    ZQueue<T> zqueue;
    ZQueueTestBase(size_t capacity = 4, size_t bw = 1) : zqueue("zqueue", *clock, 0, capacity, bw) {}
};

using ZQueueTest = ZQueueTestBase<>;

TEST_F(ZQueueTest, CanPutTakeInSameCycle)
{
    EXPECT_TRUE(zqueue.can_put());
    zqueue.put(1);
    EXPECT_TRUE(zqueue.can_take());
    EXPECT_EQ(zqueue.take(), 1);
}

TEST_F(ZQueueTest, CanPutUntilCapacity)
{
    for (unsigned i = 0; i < 4; ++i) {
        EXPECT_TRUE(zqueue.can_put());
        zqueue.put(i);
        tick();
    }

    EXPECT_FALSE(zqueue.can_put());
}

TEST_F(ZQueueTest, TakeProvidesSpace)
{
    for (unsigned i = 0; i < 4; ++i) {
        EXPECT_TRUE(zqueue.can_put());
        zqueue.put(i);
        tick();
    }

    EXPECT_FALSE(zqueue.can_put());
    EXPECT_EQ(zqueue.take(), 0);

    EXPECT_TRUE(zqueue.can_put());
}

TEST_F(ZQueueTest, ClearOnlyLater)
{
    zqueue.put(0);
    tick();
    zqueue.put(1);
    zqueue.clear();
    EXPECT_EQ(zqueue.size(), 1);
}

TEST_F(ZQueueTest, ClearIfOnlyLater)
{
    zqueue.put(0);
    tick();
    zqueue.put(1);
    tick();
    zqueue.put(2);

    zqueue.clear_if([](const auto x) { return x % 2 == 0; });
    EXPECT_EQ(zqueue.size(), 2);
}

} // namespace
