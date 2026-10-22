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

struct ZDelayWithCreditTest : public TestModule
{
    Receiver receiver{"receiver"};
    Sender sender{"sender"};

    ZDelayWithCreditTest() : TestModule{{"receiver.pool.size", 4}} {}
};

TEST_F(ZDelayWithCreditTest, SendAndReceiveInSameCycle)
{
    connect(sender.out, receiver.in).name("zdelaywithcredit").latency(0);
    start_simulation();

    ASSERT_TRUE(sender.out->can_put());
    sender.out->put(10);
    auto [data, credit] = receiver.in->take_with_credit();
    EXPECT_EQ(data, 10);
    credit->release();
    ASSERT_FALSE(receiver.in->can_take());
};

struct SendRecv : public Module
{
public:
    explicit SendRecv(const sc_core::sc_module_name& name) : Module(name)
    {
        in.set_credit_pool(&pool); // must be set before connect!
    }
    InWithCredit<int> in{"in"};
    Out<int> out{"out"};
    CreditPool pool{"pool"};
};

class ZDelayWithCreditLoopModule : public Test
{};

TEST_F(ZDelayWithCreditLoopModule, ZeroDelayLoopIsDetected)
{
    SendRecv a{"a"};
    SendRecv b{"b"};

    connect(a.out, b.in).latency(0);
    connect(b.out, a.in).latency(0);

    std::stringstream buffer;
    std::streambuf* sbuf = std::cerr.rdbuf();

    std::cerr.rdbuf(buffer.rdbuf());

    EXPECT_ANY_THROW(start_simulation());
    EXPECT_THAT(
        buffer.str(),
        MatchesRegex(
            "Zero delay loop detected! Participating nodes -> their dependency:\n\t[ab] -> [ab]\n\t[ab] -> [ab]\n"));
    std::cerr.rdbuf(sbuf);
};

} // namespace
