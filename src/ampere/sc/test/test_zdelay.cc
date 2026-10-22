//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/sc/fifo/zdelay.h"
#include "ampere/sc/connect.h"
#include "ampere/sc/module.h"
#include "ampere/sc/testing/test.h"
#include "ampere/sc/testing/assertions.h"

using namespace ampere::sc;
using ::testing::HasSubstr;
using ::testing::MatchesRegex;

namespace {

template<typename T = int>
struct ZDelayTestBase : public Test
{
    ZDelay<T> zdelay;
    ZDelayTestBase(size_t bw = 1) : zdelay("zdelay", *clock, bw) {}
};

using ZDelayTest = ZDelayTestBase<>;

TEST_F(ZDelayTest, CantReadFromEmptyZDelay)
{
    EXPECT_FALSE(zdelay.can_take());
}

TEST_F(ZDelayTest, CanPutToEmptyZDelay)
{
    EXPECT_TRUE(zdelay.can_put());
}

TEST_F(ZDelayTest, CantPutTwiceToZDelay)
{
    zdelay.put(17);
    EXPECT_FALSE(zdelay.can_put());
}

TEST_F(ZDelayTest, CanTakeImmediatelyFromFullZDelay)
{
    zdelay.put(17);
    ASSERT_TRUE(zdelay.can_take());
    EXPECT_EQ(zdelay.take(), 17);
}

TEST_F(ZDelayTest, TakeFromEmptyZDelayThrowsException)
{
    EXPECT_AMPERE_FATAL(zdelay.take(), HasSubstr("cannot take from empty zdelay"));
}

TEST_F(ZDelayTest, CannotTakeTwiceFromZDelay)
{
    zdelay.put(17);
    zdelay.take();
    EXPECT_FALSE(zdelay.can_take());
}

struct NonDefaultConstructibleObject
{
    explicit NonDefaultConstructibleObject(int value) : value{value} {}
    int value;
};

using ZDelayNonDefaultConstructibleObjectTest = ZDelayTestBase<NonDefaultConstructibleObject>;

TEST_F(ZDelayNonDefaultConstructibleObjectTest, CanPutNonDefaultConstructibleObject)
{
    ZDelay<NonDefaultConstructibleObject> zdelay("zdelay", *clock);
    zdelay.put(NonDefaultConstructibleObject{1999});
}

TEST_F(ZDelayTest, CanTakeReportsStaleData)
{
    zdelay.put(17);
    tick();
    EXPECT_AMPERE_FATAL(zdelay.can_take(), HasSubstr("stale data in zdelay fifo"));
}

TEST_F(ZDelayTest, CanPutReportsStaleData)
{
    zdelay.put(17);
    tick();
    EXPECT_AMPERE_FATAL(zdelay.can_put(), HasSubstr("stale data in zdelay fifo"));
}

TEST_F(ZDelayTest, CanTakeAfterClear)
{
    zdelay.put(411);
    zdelay.clear();
    EXPECT_TRUE(zdelay.can_take());
}

struct ZDelayBandwidthTwoTest : ZDelayTestBase<int>
{
    ZDelayBandwidthTwoTest() : ZDelayTestBase<int>(2) {}
};

TEST_F(ZDelayBandwidthTwoTest, CanPutTwiceToBandwidthTwoZDelay)
{
    zdelay.put(909);
    EXPECT_TRUE(zdelay.can_put());
}

TEST_F(ZDelayBandwidthTwoTest, Size)
{
    zdelay.put(17);
    zdelay.put(18);
    EXPECT_EQ(zdelay.size(), 2ul);
    EXPECT_EQ(zdelay.take(), 17);
    EXPECT_EQ(zdelay.size(), 1ul);
    EXPECT_EQ(zdelay.take(), 18);
    EXPECT_EQ(zdelay.size(), 0ul);
}

TEST_F(ZDelayBandwidthTwoTest, Ready)
{
    zdelay.put(17);
    EXPECT_EQ(zdelay.num_ready(), 1ul);
    zdelay.put(18);
    EXPECT_EQ(zdelay.num_ready(), 2ul);
}

TEST_F(ZDelayBandwidthTwoTest, PeekAtAllEntries)
{
    zdelay.put(17);
    zdelay.put(18);
    EXPECT_EQ(zdelay.peek(0), 17);
    EXPECT_EQ(zdelay.peek(1), 18);
    EXPECT_EQ(zdelay.take(), 17);
    EXPECT_EQ(zdelay.peek(0), 18);
}

struct ZDelayBandwidthThreeTest : ZDelayTestBase<int>
{
    ZDelayBandwidthThreeTest() : ZDelayTestBase<int>(3) {}
};

TEST_F(ZDelayBandwidthThreeTest, ZDelayDeliversDataInOrder)
{
    zdelay.put(1);
    zdelay.put(2);
    zdelay.put(3);
    EXPECT_FALSE(zdelay.can_put());
    ASSERT_TRUE(zdelay.can_take());
    EXPECT_EQ(zdelay.take(), 1);
    ASSERT_TRUE(zdelay.can_take());
    EXPECT_EQ(zdelay.take(), 2);
    ASSERT_TRUE(zdelay.can_take());
    EXPECT_EQ(zdelay.take(), 3);
    EXPECT_FALSE(zdelay.can_take());
}

struct ZDelayThingWithComplexClear
{
    int* called_ptr;
    void fifo_clear() { ++*called_ptr; }
    explicit ZDelayThingWithComplexClear(int* called) : called_ptr(called) {}
};

struct ZDelayComplexClearTest : ZDelayTestBase<ZDelayThingWithComplexClear>
{
    ZDelayComplexClearTest() : ZDelayTestBase<ZDelayThingWithComplexClear>(2) {}
};

TEST_F(ZDelayComplexClearTest, ClearWillNotInvokeFifoClearMethod)
{
    ZDelay<ZDelayThingWithComplexClear> zdelay("zdelay", *clock, 2);
    int called{0};
    ZDelayThingWithComplexClear thing1{&called};
    ZDelayThingWithComplexClear thing2{&called};
    zdelay.put(thing1);
    zdelay.put(thing2);
    zdelay.clear();
    EXPECT_EQ(called, 0);
}

class Sender : public Module
{
public:
    explicit Sender(const sc_core::sc_module_name& name) : Module(name) {}
    Out<int> out{"out"};

    bool run() override
    {
        if (sequence.empty())
            return false;

        if (get_cycle() == sequence.front().first) {
            out->put(sequence.front().second);
            sequence.pop_front();
        }

        return true;
    }

    std::deque<std::pair<Cycle, int>> sequence;
};

class Receiver : public Module
{
public:
    explicit Receiver(const sc_core::sc_module_name& name) : Module(name) {}
    In<int> in{"in"};

    bool run() override
    {
        if (in->can_take())
            received.emplace_back(get_cycle(), in->take());
        return true;
    }

    std::deque<std::pair<Cycle, int>> received;
};

class ZDelayTestModule : public Test
{};

TEST_F(ZDelayTestModule, SendAndReceiveInSameCycle)
{
    Receiver receiver{"receiver"};
    Sender sender{"sender"};

    connect(sender.out, receiver.in).name("zdelay").latency(0);

    start_simulation();

    std::pair<Cycle, int> item{0, 21};
    sender.sequence = {item};
    tick();
    ASSERT_FALSE(receiver.received.empty());
    EXPECT_EQ(receiver.received.front(), item);
}

class SendRecv : public Module
{
public:
    explicit SendRecv(const sc_core::sc_module_name& name) : Module(name) {}
    In<int> in{"in"};
    Out<int> out{"out"};
};

class ZDelayLoopModule : public Test
{};

TEST_F(ZDelayLoopModule, ZeroDelayLoopIsDetected)
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
}
} // namespace
