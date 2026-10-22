//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <gtest/gtest.h>
#include "ampere/sc/testing/test_module.h"
#include "ampere/core/testing/mock_indexer.h"
#include "ampere/core/l2/crp/nru.h"

using namespace ampere::core;

namespace ampere::test_L2CRP {

class NRUTest : public TestModule
{
    void SetUp() override
    {
        nru.l1i_lookup.bind(mock_lookup);
        nru.l1d_lookup.bind(mock_lookup);
        TestModule::SetUp();
    }

protected:
    NRU nru{"nru"};
    MockCacheLookup mock_lookup;
};

#define check_victim(expected)                                       \
    {                                                                \
        auto victim = nru.victim(0, 0, set, util::mask(set.size())); \
        ASSERT_EQ(expected, victim);                                 \
    }

#define prefetch(way) nru.touch(0, 0, way, L2ReplacementPolicy::TouchOp::REFILL, false, set)
#define fill(way) nru.touch(0, 0, way, L2ReplacementPolicy::TouchOp::REFILL, true, set)

TEST_F(NRUTest, init)
{
    Set set;

    set.resize(4);
    nru.init(1, 1, set.size(), util::mask(set.size()));

    // Round-robin should increment the victim each time when nothing else changes
    check_victim(1);
    check_victim(2);
    check_victim(3);
    check_victim(0);
    check_victim(1);
}

TEST_F(NRUTest, evict_not_recent)
{
    Set set;

    set.resize(4);
    for (int w = 0; w < set.size(); w++)
        set[w].line.state = LineState::UC;
    nru.init(1, 1, set.size(), util::mask(set.size()));

    fill(1);         // Recent 0 1 0 0
    check_victim(2); // RR 1
    fill(3);         // Recent 0 1 0 1
    check_victim(2); // RR 2
    check_victim(0); // RR 3
    fill(2);         // Recent 0 1 1 1
    check_victim(0); // RR 0
    check_victim(0); // RR 1
    fill(2);         // Recent 0 1 1 1
    check_victim(0); // RR 2
    check_victim(0); // RR 3
    fill(0);         // Recent 1 0 0 0
    check_victim(1); // RR 0
}

TEST_F(NRUTest, check_priority)
{
    Set set;

    set.resize(6);
    for (int w = 0; w < set.size(); w++)
        set[w].line.state = LineState::UC;
    nru.init(1, 1, set.size(), util::mask(set.size()));

    // way 5: invalid
    // way 4: not in L1, not recent, prefetch
    // way 3: not in L1, not recent
    // way 2: not in L1, recent
    // way 1: in l1, not recent
    // way 0: in l1, recent
    set[5].line.state = LineState::I;
    set[4].line.prefetch = true;
    // way 3 is already correct
    fill(2);
    set[1].line.in_l1d = true;
    fill(0);
    set[0].line.in_l1i = true;

    // We should choose the ways in reverse order. After each one is chosen as a victim, mark
    // it as busy to prevent it from being chosen next time.
    check_victim(5);
    set[5].line.busy = true;
    check_victim(4);
    set[4].line.busy = true;
    check_victim(3);
    set[3].line.busy = true;
    check_victim(2);
    set[2].line.busy = true;
    check_victim(1);
    set[1].line.busy = true;
    check_victim(0);
}

TEST_F(NRUTest, prefetch_no_set_recent)
{
    Set set;

    set.resize(4);
    for (int w = 0; w < set.size(); w++)
        set[w].line.state = LineState::UC;
    nru.init(1, 1, set.size(), util::mask(set.size()));

    fill(1);         // Recent 0 1 0 0
    check_victim(2); // RR 1
    prefetch(3);     // Recent 0 1 0 0
    check_victim(2); // RR 2
    check_victim(3); // RR 3
    fill(0);         // Recent 1 1 0 0
    check_victim(2); // RR 0
    fill(3);         // Recent 1 1 0 1
    check_victim(2); // RR 1
    check_victim(2); // RR 2
    prefetch(2);     // Recent 0 0 0 0
    check_victim(3); // RR 3
    check_victim(0); // RR 0
    check_victim(1); // RR 1
    check_victim(2); // RR 2
    check_victim(3); // RR 3
}

} // namespace ampere::test_L2CRP
