//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <gtest/gtest.h>
#include "ampere/sc/testing/test_module.h"
#include "ampere/core/testing/mock_indexer.h"
#include "ampere/core/l2/crp/rrip.h"

using namespace ampere::core;

namespace ampere::test_L2CRP {

class RRIPTest : public TestModule
{
public:
    RRIPTest() : TestModule() {}

    RRIPTest(std::initializer_list<std::pair<std::string, sc::json>> settings) : TestModule(settings) {}

    void SetUp() override
    {
        rrip.l1i_lookup.bind(mock_lookup);
        rrip.l1d_lookup.bind(mock_lookup);
        TestModule::SetUp();
    }

    L2RRIP rrip{"rrip"};
    MockCacheLookup mock_lookup;
};

class L2RRIPIgnoreL1ITest : public RRIPTest
{
public:
    L2RRIPIgnoreL1ITest() : RRIPTest({{"rrip.ignore_in_l1i", true}, {"rrip.l1_priority", false}}) {}
};

class L2RRIPUseL1ITest : public RRIPTest
{
public:
    L2RRIPUseL1ITest() : RRIPTest({{"rrip.ignore_in_l1i", false}, {"rrip.l1_priority", false}}) {}
};

class L2RRIPIgnoreL1DTest : public RRIPTest
{
public:
    L2RRIPIgnoreL1DTest() : RRIPTest({{"rrip.ignore_in_l1d", true}, {"rrip.l1_priority", false}}) {}
};

class L2RRIPUseL1DTest : public RRIPTest
{
public:
    L2RRIPUseL1DTest() : RRIPTest({{"rrip.ignore_in_l1d", false}, {"rrip.l1_priority", false}}) {}
};

class L2RRIPL1Priority : public RRIPTest
{
public:
    L2RRIPL1Priority()
        : RRIPTest({{"rrip.ignore_in_l1d", true}, {"rrip.ignore_in_l1i", true}, {"rrip.l1_priority", true}})
    {
    }
};

#define check_victim(expected)                                        \
    {                                                                 \
        auto victim = rrip.victim(0, 0, set, util::mask(set.size())); \
        ASSERT_EQ(expected, victim);                                  \
    }

#define prefetch(way) rrip.touch(0, 0, way, L2ReplacementPolicy::TouchOp::REFILL, false, set)
#define fill(way) rrip.touch(0, 0, way, L2ReplacementPolicy::TouchOp::REFILL, true, set)
#define hit(way) rrip.touch(0, 0, way, L2ReplacementPolicy::TouchOp::LOOKUP, true, set)

TEST_F(RRIPTest, init)
{
    Set set;

    set.resize(4);
    rrip.init(1, 1, set.size(), util::mask(set.size()));

    // Round-robin should increment the victim each time when nothing else changes
    // Start with ages 0 0 0 0
    check_victim(1); // ages 3 3 3 3
    check_victim(2);
    check_victim(3);
    check_victim(0);
    check_victim(1);
}

TEST_F(RRIPTest, fill_and_hit)
{
    Set set;

    set.resize(4);
    for (int w = 0; w < set.size(); w++)
        set[w].line.state = LineState::UC;
    rrip.init(1, 1, set.size(), util::mask(set.size()));

    fill(1);         // Ages 0 2 0 0
    check_victim(1); // Ages 1 3 1 1, RR 1
    check_victim(1); // RR 2
    fill(0);         // Ages 2 3 1 1
    fill(1);         // Ages 2 2 1 1
    check_victim(0); // Ages 3 3 2 2, RR 3
    check_victim(0); // RR 0
    check_victim(1); // RR 1
    check_victim(0); // RR 2
    hit(0);          // Ages 0 3 2 2
    check_victim(1); // RR 3
    hit(1);          // Ages 0 0 2 2
    check_victim(2); // Ages 1 1 3 3, RR 0
    hit(2);          // Ages 1 1 0 3
    check_victim(3); // RR 1
    check_victim(3); // RR 2
    check_victim(3); // RR 3
    check_victim(3); // RR 0
    prefetch(0);     // Ages 3 1 0 3
    check_victim(3); // RR 1
    check_victim(3); // RR 2
    check_victim(3); // RR 3
    check_victim(0); // RR 0
}

TEST_F(L2RRIPIgnoreL1ITest, ignore_in_l1i)
{
    Set set;

    set.resize(4);
    for (int w = 0; w < set.size(); w++)
        set[w].line.state = LineState::UC;
    rrip.init(1, 1, set.size(), util::mask(set.size()));

    check_victim(1); // Ages 3 3 3 3
    check_victim(2);
    check_victim(3);
    check_victim(0);

    set[1].line.in_l1i = true;
    check_victim(1); // choose victim 1 despite in_l1i
}

TEST_F(L2RRIPUseL1ITest, use_in_l1i)
{
    Set set;

    set.resize(4);
    for (int w = 0; w < set.size(); w++)
        set[w].line.state = LineState::UC;
    rrip.init(1, 1, set.size(), util::mask(set.size()));

    check_victim(1); // Ages 3 3 3 3
    check_victim(2);
    check_victim(3);
    check_victim(0);

    set[1].line.in_l1i = true;
    check_victim(2); // skip victim 1
}

TEST_F(L2RRIPIgnoreL1DTest, ignore_in_l1d)
{
    Set set;

    set.resize(4);
    for (int w = 0; w < set.size(); w++)
        set[w].line.state = LineState::UC;
    rrip.init(1, 1, set.size(), util::mask(set.size()));

    check_victim(1); // Ages 3 3 3 3
    check_victim(2);
    check_victim(3);
    check_victim(0);

    set[1].line.in_l1d = true;
    check_victim(1); // choose victim 1 despite in_l1d
}

TEST_F(L2RRIPUseL1DTest, use_in_l1d)
{
    Set set;

    set.resize(4);
    for (int w = 0; w < set.size(); w++)
        set[w].line.state = LineState::UC;
    rrip.init(1, 1, set.size(), util::mask(set.size()));

    check_victim(1); // Ages 3 3 3 3
    check_victim(2);
    check_victim(3);
    check_victim(0);

    set[1].line.in_l1d = true;
    check_victim(2); // skip victim 1
}

TEST_F(L2RRIPL1Priority, l1_priority)
{
    Set set;

    set.resize(4);
    for (int w = 0; w < set.size(); w++)
        set[w].line.state = LineState::UC;
    rrip.init(1, 1, set.size(), util::mask(set.size()));

    check_victim(1); // Ages 3 3 3 3, RR 1
    hit(1);
    hit(2);
    hit(3); // Ages 3 0 0 0
    set[0].line.in_l1d = true;
    check_victim(0); // RR 2; 0 is oldest so choose it even though in L1
    hit(0);          // Ages 0 0 0 0
    check_victim(3); // Ages 3 3 3 3, RR 3
    check_victim(1); // RR 0; skip 0 since in L1 and another way at same age
}

} // namespace ampere::test_L2CRP
