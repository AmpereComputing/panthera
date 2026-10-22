//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <gtest/gtest.h>
#include "ampere/sc/testing/test_module.h"
#include "ampere/core/testing/mock_indexer.h"
#include "ampere/core/l2/crp/drrip.h"

using namespace ampere::core;

namespace ampere::test_L2CRP {

class DRRIPTest : public TestModule
{
public:
    DRRIPTest() : TestModule() {}

    DRRIPTest(std::initializer_list<std::pair<std::string, sc::json>> settings) : TestModule(settings) {}

    void SetUp() override
    {
        rrip.l1i_lookup.bind(mock_lookup);
        rrip.l1d_lookup.bind(mock_lookup);
        TestModule::SetUp();
    }

    L2DRRIP rrip{"rrip"};
    MockCacheLookup mock_lookup;
};

class L2DRRIPShort : public DRRIPTest
{
public:
    L2DRRIPShort()
        : DRRIPTest({{"rrip.brrip_long_frequency", 4},
                     {"rrip.set_dueling_mask", 0x3},
                     {"rrip.dedicated_srrip_match", 0x0},
                     {"rrip.dedicated_brrip_match", 0x3},
                     {"rrip.psel_counter_bits", 2}})
    {
    }
};

#define check_victim(index, expected)                                                 \
    {                                                                                 \
        auto victim = rrip.victim(0, index, sets[index], util::mask(sets[0].size())); \
        ASSERT_EQ(expected, victim);                                                  \
    }

#define prefetch(set, way) rrip.touch(0, set, way, L2ReplacementPolicy::TouchOp::REFILL, false, sets[set])
#define fill(set, way) rrip.touch(0, set, way, L2ReplacementPolicy::TouchOp::REFILL, true, sets[set])
#define hit(set, way) rrip.touch(0, set, way, L2ReplacementPolicy::TouchOp::LOOKUP, true, sets[set])

TEST_F(DRRIPTest, init)
{
    Set sets[1];

    sets[0].resize(4);
    rrip.init(1, 1, sets[0].size(), util::mask(sets[0].size()));

    // Round-robin should increment the victim each time when nothing else changes
    // Start with ages 0 0 0 0
    check_victim(0, 1); // ages 3 3 3 3
    check_victim(0, 2);
    check_victim(0, 3);
    check_victim(0, 0);
    check_victim(0, 1);
}

TEST_F(L2DRRIPShort, fill_and_hit)
{
    Set sets[4];

    for (int s = 0; s < 4; s++) {
        sets[s].resize(4);
        for (int w = 0; w < sets[0].size(); w++)
            sets[s][w].line.state = LineState::UC;
    }
    rrip.init(1, 4, sets[0].size(), util::mask(sets[0].size()));

    // Set 0 is dedicated SRRIP
    // Set 3 is dedicated BRRIP
    // Sets 1 and 2 use the current policy (starts as SRRIP)

    // First check that set 1 acts as SRRIP (same test as for basic RRIP)
    fill(1, 1);         // Ages 0 2 0 0
    check_victim(1, 1); // Ages 1 3 1 1, RR 1
    check_victim(1, 1); // RR 2
    fill(1, 0);         // Ages 2 3 1 1
    fill(1, 1);         // Ages 2 2 1 1
    check_victim(1, 0); // Ages 3 3 2 2, RR 3
    check_victim(1, 0); // RR 0
    check_victim(1, 1); // RR 1
    check_victim(1, 0); // RR 2
    hit(1, 0);          // Ages 0 3 2 2
    check_victim(1, 1); // RR 3
    hit(1, 1);          // Ages 0 0 2 2
    check_victim(1, 2); // Ages 1 1 3 3, RR 0
    hit(1, 2);          // Ages 1 1 0 3
    check_victim(1, 3); // RR 1
    check_victim(1, 3); // RR 2
    check_victim(1, 3); // RR 3
    check_victim(1, 3); // RR 0
    prefetch(1, 0);     // Ages 3 1 0 3
    check_victim(1, 3); // RR 1
    check_victim(1, 3); // RR 2
    check_victim(1, 3); // RR 3
    check_victim(1, 0); // RR 0

    // Now check that set 0 acts as SRRIP (same test as above)
    fill(0, 1);         // Ages 0 2 0 0
    check_victim(0, 1); // Ages 1 3 1 1, RR 1
    check_victim(0, 1); // RR 2
    fill(0, 0);         // Ages 2 3 1 1
    fill(0, 1);         // Ages 2 2 1 1
    check_victim(0, 0); // Ages 3 3 2 2, RR 3
    check_victim(0, 0); // RR 0
    check_victim(0, 1); // RR 1
    check_victim(0, 0); // RR 2
    hit(0, 0);          // Ages 0 3 2 2
    check_victim(0, 1); // RR 3
    hit(0, 1);          // Ages 0 0 2 2
    check_victim(0, 2); // Ages 1 1 3 3, RR 0
    hit(0, 2);          // Ages 1 1 0 3
    check_victim(0, 3); // RR 1
    check_victim(0, 3); // RR 2
    check_victim(0, 3); // RR 3
    check_victim(0, 3); // RR 0
    prefetch(0, 0);     // Ages 3 1 0 3
    check_victim(0, 3); // RR 1
    check_victim(0, 3); // RR 2
    check_victim(0, 3); // RR 3
    check_victim(0, 0); // RR 0

    // The fills into the dedicated SRRIP set will have pushed sets 1 and 2 to use BRRIP.
    // Check that set 2 acts as BRRIP (test from BRRIP module)
    fill(2, 1);         // Ages 0 3 0 0, count 1
    check_victim(2, 1); // Ages 0 3 0 0, RR 1
    check_victim(2, 1); // RR 2
    fill(2, 0);         // Ages 3 3 0 0, count 2
    fill(2, 1);         // Ages 3 3 0 0, count 3
    check_victim(2, 0); // Ages 3 3 0 0, RR 3
    check_victim(2, 0); // RR 0
    check_victim(2, 1); // RR 1
    check_victim(2, 0); // RR 2
    fill(2, 2);         // Ages 3 3 2 0, count 0 -- fill at age 2
    check_victim(2, 0); // RR 3
    check_victim(2, 0); // RR 0
    check_victim(2, 1); // RR 1
    check_victim(2, 0); // RR 2
    hit(2, 0);          // Ages 0 3 2 0
    hit(2, 1);          // Ages 0 0 2 0
    check_victim(2, 2); // Ages 1 1 3 1, RR 3
    check_victim(2, 2); // Ages 1 1 3 1, RR 0
    check_victim(2, 2); // Ages 1 1 3 1, RR 1
    fill(2, 3);         // Ages 1 1 3 3, count 1
    check_victim(2, 2); // Ages 1 1 3 3, RR 2
    check_victim(2, 3); // Ages 1 1 3 3, RR 3
    check_victim(2, 2); // Ages 1 1 3 3, RR 0

    // Reset the brrip counter
    fill(2, 3); // Ages 1 1 3 3, count 2
    fill(2, 3); // Ages 1 1 3 3, count 3
    fill(2, 3); // Ages 1 1 3 3, count 0

    // Check that set 3 acts as BRRIP (test from BRRIP module)
    fill(3, 1);         // Ages 0 3 0 0, count 1
    check_victim(3, 1); // Ages 0 3 0 0, RR 1
    check_victim(3, 1); // RR 2
    fill(3, 0);         // Ages 3 3 0 0, count 2
    fill(3, 1);         // Ages 3 3 0 0, count 3
    check_victim(3, 0); // Ages 3 3 0 0, RR 3
    check_victim(3, 0); // RR 0
    check_victim(3, 1); // RR 1
    check_victim(3, 0); // RR 2
    fill(3, 2);         // Ages 3 3 2 0, count 0 -- fill at age 2
    check_victim(3, 0); // RR 3
    check_victim(3, 0); // RR 0
    check_victim(3, 1); // RR 1
    check_victim(3, 0); // RR 2
    hit(3, 0);          // Ages 0 3 2 0
    hit(3, 1);          // Ages 0 0 2 0
    check_victim(3, 2); // Ages 1 1 3 1, RR 3
    check_victim(3, 2); // Ages 1 1 3 1, RR 0
    check_victim(3, 2); // Ages 1 1 3 1, RR 1
    fill(3, 3);         // Ages 1 1 3 3
    check_victim(3, 2); // Ages 1 1 3 3, RR 2
    check_victim(3, 3); // Ages 1 1 3 3, RR 3
    check_victim(3, 2); // Ages 1 1 3 3, RR 0

    // Reset the brrip counter
    fill(3, 3); // Ages 1 1 3 3, count 2
    fill(3, 3); // Ages 1 1 3 3, count 3
    fill(3, 3); // Ages 1 1 3 3, count 0

    // The fills to the dedicated BRRIP set will have pushed sets 1 and 2 back to using SRRIP.
    // Reset that ages of set 2, then verify it now uses SRRIP.
    hit(2, 0);
    hit(2, 1);
    hit(2, 2);
    hit(2, 3);

    // SRRIP test
    fill(2, 1);         // Ages 0 2 0 0
    check_victim(2, 1); // Ages 1 3 1 1, RR 1
    check_victim(2, 1); // RR 2
    fill(2, 0);         // Ages 2 3 1 1
    fill(2, 1);         // Ages 2 2 1 1
    check_victim(2, 0); // Ages 3 3 2 2, RR 3
    check_victim(2, 0); // RR 0
    check_victim(2, 1); // RR 1
    check_victim(2, 0); // RR 2
    hit(2, 0);          // Ages 0 3 2 2
    check_victim(2, 1); // RR 3
    hit(2, 1);          // Ages 0 0 2 2
    check_victim(2, 2); // Ages 1 1 3 3, RR 0
    hit(2, 2);          // Ages 1 1 0 3
    check_victim(2, 3); // RR 1
    check_victim(2, 3); // RR 2
    check_victim(2, 3); // RR 3
    check_victim(2, 3); // RR 0
    prefetch(2, 0);     // Ages 3 1 0 3
    check_victim(2, 3); // RR 1
    check_victim(2, 3); // RR 2
    check_victim(2, 3); // RR 3
    check_victim(2, 0); // RR 0
}

} // namespace ampere::test_L2CRP
