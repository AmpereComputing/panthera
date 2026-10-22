//
// Copyright (c) 2024 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include <gtest/gtest.h>

#include "ampere/sc/connect.h"
#include "ampere/sc/testing/test_module.h"

using namespace ampere::sc;
using ::testing::MatchesRegex;

namespace {

struct Sender : Module
{
    explicit Sender(const sc_core::sc_module_name& name) : Module(name) {}
    Out<int> out{"out"};
};

struct Receiver : public Module
{
    explicit Receiver(const sc_core::sc_module_name& name) : Module(name)
    {
        in.set_credit_pool(&pool); // must be set before connect!
    }
    InWithCredit<int> in{"in"};
    CreditPool pool{"pool"};
};

struct ZQueueWithCreditTest : public TestModule
{
    Receiver receiver{"receiver"};
    Sender sender{"sender"};

    ZQueueWithCreditTest() : TestModule{{"receiver.pool.size", 4}} {}

    void SetUp() override
    {
        connect(sender.out, receiver.in).name("zqueuewithcredit").latency(0).capacity(4);
        start_simulation();
    }
};

TEST_F(ZQueueWithCreditTest, CanPutTakeInSameCycle)
{
    EXPECT_TRUE(sender.out->can_put());
    sender.out->put(1);
    EXPECT_TRUE(receiver.in->can_take());
    EXPECT_EQ(receiver.in->take(), 1);
}

TEST_F(ZQueueWithCreditTest, CanPutUntilCapacity)
{
    for (unsigned i = 0; i < 4; ++i) {
        EXPECT_TRUE(sender.out->can_put());
        sender.out->put(10);
        tick();
    }

    EXPECT_FALSE(sender.out->can_put());
}

TEST_F(ZQueueWithCreditTest, TakeProvidesSpace)
{
    for (unsigned i = 0; i < 4; ++i) {
        EXPECT_TRUE(sender.out->can_put());
        sender.out->put(i);
        tick();
    }

    auto [data, credit] = receiver.in->take_with_credit();
    EXPECT_EQ(data, 0);
    credit->release();

    EXPECT_TRUE(sender.out->can_put());
};

TEST_F(ZQueueWithCreditTest, ClearOnlyLater)
{
    sender.out->put(0);
    tick();
    sender.out->put(1);

    receiver.in->clear();
    EXPECT_EQ(sender.out->size(), 1);
}

TEST_F(ZQueueWithCreditTest, ClearIfOnlyLater)
{
    sender.out->put(0);
    tick();
    sender.out->put(1);
    tick();
    sender.out->put(2);

    receiver.in->clear_if([](const auto x) { return x % 2 == 0; });
    EXPECT_EQ(sender.out->size(), 2);
}

} // namespace
