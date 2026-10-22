//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <gtest/gtest.h>
#include "ampere/sc/testing/test_module.h"
#include "ampere/uarch_common/crp/plru5.h"

using namespace ampere::sc;

namespace ampere::uarch_common {

class PLRU5Test : public TestModule
{
protected:
    PLRU5 plru5{"plru5"};
};

TEST_F(PLRU5Test, init)
{
    plru5.init(1, 5);
    ASSERT_EQ(0, plru5.victim(0));
}

TEST_F(PLRU5Test, untouched_victim)
{
    plru5.init(1, 5);
    ASSERT_EQ(0, plru5.victim(0));
}

TEST_F(PLRU5Test, oldest_touched_victim)
{
    plru5.init(1, 5);
    // Comments are group pointer, LRU pointer within each group (group C is always 0)
    // Start: A 000
    plru5.touch(0, 0, CacheReplacementPolicy::TouchOp::LOOKUP); // B 100
    plru5.touch(0, 2, CacheReplacementPolicy::TouchOp::LOOKUP); // C 110
    plru5.touch(0, 3, CacheReplacementPolicy::TouchOp::LOOKUP); // C 100
    plru5.touch(0, 1, CacheReplacementPolicy::TouchOp::LOOKUP); // C 000
    plru5.touch(0, 4, CacheReplacementPolicy::TouchOp::LOOKUP); // A 000
    plru5.touch(0, 4, CacheReplacementPolicy::TouchOp::LOOKUP); // A 000
    plru5.touch(0, 2, CacheReplacementPolicy::TouchOp::LOOKUP); // A 010
    plru5.touch(0, 1, CacheReplacementPolicy::TouchOp::LOOKUP); // B 010
    plru5.touch(0, 2, CacheReplacementPolicy::TouchOp::LOOKUP); // C 010
    ASSERT_EQ(4, plru5.victim(0));
}

TEST_F(PLRU5Test, victim_after_victim)
{
    plru5.init(1, 5);
    // Comments are group pointer, LRU pointer within each group (group C is always 0)
    // Start: A 000
    plru5.touch(0, 0, CacheReplacementPolicy::TouchOp::REFILL); // B 100
    ASSERT_EQ(2, plru5.victim(0));
    plru5.touch(0, 2, CacheReplacementPolicy::TouchOp::REFILL); // C 110
    ASSERT_EQ(4, plru5.victim(0));
    plru5.touch(0, 3, CacheReplacementPolicy::TouchOp::REFILL); // C 100
    ASSERT_EQ(4, plru5.victim(0));
    plru5.touch(0, 1, CacheReplacementPolicy::TouchOp::REFILL); // C 000
    ASSERT_EQ(4, plru5.victim(0));
    plru5.touch(0, 4, CacheReplacementPolicy::TouchOp::REFILL); // A 000
    ASSERT_EQ(0, plru5.victim(0));
    plru5.touch(0, 4, CacheReplacementPolicy::TouchOp::REFILL); // A 000
    ASSERT_EQ(0, plru5.victim(0));
    plru5.touch(0, 2, CacheReplacementPolicy::TouchOp::REFILL); // A 010
    ASSERT_EQ(0, plru5.victim(0));
    plru5.touch(0, 4, CacheReplacementPolicy::TouchOp::REFILL); // A 010
    ASSERT_EQ(0, plru5.victim(0));
    plru5.touch(0, 4, CacheReplacementPolicy::TouchOp::REFILL); // A 010
    ASSERT_EQ(0, plru5.victim(0));
    plru5.touch(0, 1, CacheReplacementPolicy::TouchOp::REFILL); // B 110
    ASSERT_EQ(3, plru5.victim(0));
    plru5.touch(0, 2, CacheReplacementPolicy::TouchOp::REFILL); // C 110
    ASSERT_EQ(4, plru5.victim(0));
    plru5.touch(0, 2, CacheReplacementPolicy::TouchOp::REFILL); // C 110
    ASSERT_EQ(4, plru5.victim(0));
    plru5.touch(0, 3, CacheReplacementPolicy::TouchOp::REFILL); // C 100
    ASSERT_EQ(4, plru5.victim(0));
    plru5.touch(0, 1, CacheReplacementPolicy::TouchOp::REFILL); // C 000
    ASSERT_EQ(4, plru5.victim(0));
    plru5.touch(0, 4, CacheReplacementPolicy::TouchOp::REFILL); // A 000
    ASSERT_EQ(0, plru5.victim(0));
    plru5.touch(0, 0, CacheReplacementPolicy::TouchOp::REFILL); // B 100
    ASSERT_EQ(2, plru5.victim(0));
    plru5.touch(0, 1, CacheReplacementPolicy::TouchOp::REFILL); // B 000
    ASSERT_EQ(2, plru5.victim(0));
    plru5.touch(0, 2, CacheReplacementPolicy::TouchOp::REFILL); // C 010
    ASSERT_EQ(4, plru5.victim(0));
    plru5.touch(0, 1, CacheReplacementPolicy::TouchOp::REFILL); // C 010
    ASSERT_EQ(4, plru5.victim(0));
    plru5.touch(0, 3, CacheReplacementPolicy::TouchOp::REFILL); // C 000
    ASSERT_EQ(4, plru5.victim(0));
    plru5.touch(0, 1, CacheReplacementPolicy::TouchOp::REFILL); // C 000
    ASSERT_EQ(4, plru5.victim(0));
    plru5.touch(0, 4, CacheReplacementPolicy::TouchOp::REFILL); // A 000
    ASSERT_EQ(0, plru5.victim(0));
}

} // namespace ampere::uarch_common
