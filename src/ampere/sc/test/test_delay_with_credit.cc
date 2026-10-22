//
// Copyright (c) 2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include <gtest/gtest.h>

#include "ampere/sc/connect.h"
#include "ampere/sc/testing/test_module.h"
#include "ampere/sc/testing/assertions.h"

using ::testing::HasSubstr;

using namespace ampere::sc;

struct DelayWithCreditTop : public Module
{
    explicit DelayWithCreditTop(const sc_core::sc_module_name& name) : Module{name}
    {
        in.set_credit_pool(&pool); // must be set before connect!
        connect(out, in);
    }

    InWithCredit<int> in{"in"};
    Out<int> out{"out"};
    CreditPool pool{"pool"};
};

struct DelayWithCreditTest : public TestModule
{
    DelayWithCreditTest() : TestModule{{"top.pool.size", 4}} {}

    void SetUp() override { start_simulation(); }

    void TearDown() override
    {
        while (!credits.empty()) {
            credits.front()->release();
            credits.pop_front();
        }
    }

    DelayWithCreditTop top{"top"};

    std::deque<int> queue;
    std::deque<Credit> credits;

    void enqueue(std::pair<int, Credit> stuff)
    {
        queue.push_back(stuff.first);
        credits.push_back(std::move(stuff.second));
    }

    void enqueue_and_return_credit(std::pair<int, Credit> stuff)
    {
        queue.push_back(stuff.first);
        top.in->put_credit(std::move(stuff.second));
    }
};

TEST_F(DelayWithCreditTest, CanSendToQueue)
{
    ASSERT_TRUE(top.out->can_put());
    top.out->put(42);
    tick(1);
    ASSERT_TRUE(top.in->can_take());
    auto [data, credit] = top.in->take_with_credit();
    EXPECT_EQ(data, 42);
    credit->release();
}

TEST_F(DelayWithCreditTest, CanFillQueue)
{
    top.out->put(10);
    tick(1);
    enqueue(top.in->take_with_credit());
    top.out->put(11);
    tick(1);
    enqueue(top.in->take_with_credit());
    top.out->put(12);
    tick(1);
    enqueue(top.in->take_with_credit());
    top.out->put(13);
    tick(1);
    enqueue(top.in->take_with_credit());
    ASSERT_FALSE(top.out->can_put());

    int result = 10;
    for (auto& item : queue)
        EXPECT_EQ(item, result++);
}

TEST_F(DelayWithCreditTest, CantPutTwiceInSameCycle)
{
    top.out->put(10);
    ASSERT_FALSE(top.out->can_put());
    EXPECT_AMPERE_FATAL(top.out->put(11), HasSubstr("cannot put to a full fifo"));
}

TEST_F(DelayWithCreditTest, CanSustainCommunication)
{
    for (int i = 0; i < 100; i++) {
        top.out->put(i);
        tick();
        enqueue_and_return_credit(top.in->take_with_credit());
        queue.pop_front();
    }
}
