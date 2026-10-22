//
// Copyright (c) 2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <gtest/gtest.h>
#include "ampere/sc/testing/test_module.h"
#include "ampere/uarch_common/crp/nmru.h"

using namespace ampere::sc;

namespace ampere::uarch_common {

class NMRUTest : public TestModule
{
protected:
    NMRU nmru{"nmru"};
};

TEST_F(NMRUTest, init)
{
    nmru.init(1, 4);
    nmru.touch(0, 1, CacheReplacementPolicy::TouchOp::REFILL);
    ASSERT_EQ(2, nmru.victim(0));
}

TEST_F(NMRUTest, oldest_touched_victim)
{
    nmru.init(1, 4);
    nmru.touch(0, 0, CacheReplacementPolicy::TouchOp::REFILL);
    nmru.touch(0, 2, CacheReplacementPolicy::TouchOp::REFILL);
    nmru.touch(0, 3, CacheReplacementPolicy::TouchOp::REFILL);
    nmru.touch(0, 1, CacheReplacementPolicy::TouchOp::REFILL);
    nmru.touch(0, 2, CacheReplacementPolicy::TouchOp::REFILL);
    nmru.touch(0, 1, CacheReplacementPolicy::TouchOp::REFILL);
    nmru.touch(0, 2, CacheReplacementPolicy::TouchOp::REFILL);
    nmru.touch(0, 2, CacheReplacementPolicy::TouchOp::REFILL);
    nmru.touch(0, 3, CacheReplacementPolicy::TouchOp::REFILL);
    nmru.touch(0, 1, CacheReplacementPolicy::TouchOp::REFILL);
    nmru.touch(0, 0, CacheReplacementPolicy::TouchOp::REFILL);
    nmru.touch(0, 1, CacheReplacementPolicy::TouchOp::REFILL);
    nmru.touch(0, 2, CacheReplacementPolicy::TouchOp::REFILL);
    nmru.touch(0, 1, CacheReplacementPolicy::TouchOp::REFILL);
    nmru.touch(0, 3, CacheReplacementPolicy::TouchOp::REFILL);
    nmru.touch(0, 1, CacheReplacementPolicy::TouchOp::REFILL);
    ASSERT_EQ(0, nmru.victim(0));
}

TEST_F(NMRUTest, victim_after_victim)
{
    nmru.init(1, 4);                                           //  Ways         Start Way
    nmru.touch(0, 0, CacheReplacementPolicy::TouchOp::REFILL); //  1 0 0 0      1
    ASSERT_EQ(1, nmru.victim(0));
    nmru.touch(0, 2, CacheReplacementPolicy::TouchOp::REFILL); //  1 0 1 0      2
    ASSERT_EQ(3, nmru.victim(0));
    nmru.touch(0, 3, CacheReplacementPolicy::TouchOp::REFILL); //  1 0 1 1      3
    ASSERT_EQ(1, nmru.victim(0));
    nmru.touch(0, 1, CacheReplacementPolicy::TouchOp::REFILL); //  0 1 0 0      0
    ASSERT_EQ(0, nmru.victim(0));
    nmru.touch(0, 2, CacheReplacementPolicy::TouchOp::REFILL); //  0 1 1 0      1
    ASSERT_EQ(3, nmru.victim(0));
    nmru.touch(0, 1, CacheReplacementPolicy::TouchOp::REFILL); //  0 1 1 0      2
    ASSERT_EQ(3, nmru.victim(0));
    nmru.touch(0, 2, CacheReplacementPolicy::TouchOp::REFILL); //  0 1 1 0      3
    ASSERT_EQ(3, nmru.victim(0));
    nmru.touch(0, 2, CacheReplacementPolicy::TouchOp::REFILL); //  0 1 1 0      0
    ASSERT_EQ(0, nmru.victim(0));
    nmru.touch(0, 3, CacheReplacementPolicy::TouchOp::REFILL); //  0 1 1 1      1
    ASSERT_EQ(0, nmru.victim(0));
    nmru.touch(0, 1, CacheReplacementPolicy::TouchOp::REFILL); //  0 1 1 1      2
    ASSERT_EQ(0, nmru.victim(0));
    nmru.touch(0, 0, CacheReplacementPolicy::TouchOp::REFILL); //  1 0 0 0      3
    ASSERT_EQ(3, nmru.victim(0));
    nmru.touch(0, 1, CacheReplacementPolicy::TouchOp::REFILL); //  1 1 0 0      0
    ASSERT_EQ(2, nmru.victim(0));
    nmru.touch(0, 2, CacheReplacementPolicy::TouchOp::REFILL); //  1 1 1 0      1
    ASSERT_EQ(3, nmru.victim(0));
    nmru.touch(0, 1, CacheReplacementPolicy::TouchOp::REFILL); //  1 1 1 0      2
    ASSERT_EQ(3, nmru.victim(0));
    nmru.touch(0, 3, CacheReplacementPolicy::TouchOp::REFILL); //  0 0 0 1      3
    ASSERT_EQ(0, nmru.victim(0));
    nmru.touch(0, 1, CacheReplacementPolicy::TouchOp::REFILL); //  0 1 0 1      0
    ASSERT_EQ(0, nmru.victim(0));
}

} // namespace ampere::uarch_common
