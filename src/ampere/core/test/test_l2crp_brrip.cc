//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <gtest/gtest.h>
#include "ampere/sc/testing/test_module.h"
#include "ampere/core/testing/mock_indexer.h"
#include "ampere/core/l2/crp/brrip.h"

using namespace ampere::core;

namespace ampere::test_L2CRP {

class BRRIPTest : public TestModule
{
public:
    BRRIPTest() : TestModule() {}

    BRRIPTest(std::initializer_list<std::pair<std::string, sc::json>> settings) : TestModule(settings) {}

    void SetUp() override
    {
        rrip.l1i_lookup.bind(mock_lookup);
        rrip.l1d_lookup.bind(mock_lookup);
        TestModule::SetUp();
    }

    L2BRRIP rrip{"rrip"};
    MockCacheLookup mock_lookup;
};

class L2BRRIPInterval4 : public BRRIPTest
{
public:
    L2BRRIPInterval4() : BRRIPTest({{"rrip.brrip_long_frequency", 4}}) {}
};

#define check_victim(expected)                                        \
    {                                                                 \
        auto victim = rrip.victim(0, 0, set, util::mask(set.size())); \
        ASSERT_EQ(expected, victim);                                  \
    }

#define prefetch(way) rrip.touch(0, 0, way, L2ReplacementPolicy::TouchOp::REFILL, false, set)
#define fill(way) rrip.touch(0, 0, way, L2ReplacementPolicy::TouchOp::REFILL, true, set)
#define hit(way) rrip.touch(0, 0, way, L2ReplacementPolicy::TouchOp::LOOKUP, true, set)

TEST_F(BRRIPTest, init)
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

TEST_F(L2BRRIPInterval4, fill_and_hit)
{
    Set set;

    set.resize(4);
    for (int w = 0; w < set.size(); w++)
        set[w].line.state = LineState::UC;
    rrip.init(1, 1, set.size(), util::mask(set.size()));

    fill(1);         // Ages 0 3 0 0, count 1
    check_victim(1); // Ages 0 3 0 0, RR 1
    check_victim(1); // RR 2
    fill(0);         // Ages 3 3 0 0, count 2
    fill(1);         // Ages 3 3 0 0, count 3
    check_victim(0); // Ages 3 3 0 0, RR 3
    check_victim(0); // RR 0
    check_victim(1); // RR 1
    check_victim(0); // RR 2
    fill(2);         // Ages 3 3 2 0, count 0 -- fill at age 2
    check_victim(0); // RR 3
    check_victim(0); // RR 0
    check_victim(1); // RR 1
    check_victim(0); // RR 2
    hit(0);          // Ages 0 3 2 0
    hit(1);          // Ages 0 0 2 0
    check_victim(2); // Ages 1 1 3 1, RR 3
    check_victim(2); // Ages 1 1 3 1, RR 0
    check_victim(2); // Ages 1 1 3 1, RR 1
    fill(3);         // Ages 1 1 3 3
    check_victim(2); // Ages 1 1 3 3, RR 2
    check_victim(3); // Ages 1 1 3 3, RR 3
    check_victim(2); // Ages 1 1 3 3, RR 0
}

} // namespace ampere::test_L2CRP
