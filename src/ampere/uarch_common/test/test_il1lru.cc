//
// Copyright (c) 2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <gtest/gtest.h>
#include "ampere/sc/testing/test_module.h"
#include "ampere/uarch_common/crp/il1_lru.h"

using namespace ampere::sc;

namespace ampere::uarch_common {

class IL1LRUTest : public TestModule
{
protected:
    IL1LRU il1_lru{"il1_lru"};
};

TEST_F(IL1LRUTest, init)
{
    il1_lru.init(1, 4);
    ASSERT_EQ(0, il1_lru.victim(0));
}

TEST_F(IL1LRUTest, untouched_victim)
{
    il1_lru.init(1, 4);
    ASSERT_EQ(0, il1_lru.victim(0));
}

TEST_F(IL1LRUTest, oldest_touched_victim)
{
    il1_lru.init(1, 4);
    il1_lru.touch(0, 3, CacheReplacementPolicy::TouchOp::REFILL);
    il1_lru.touch(0, 2, CacheReplacementPolicy::TouchOp::REFILL);
    il1_lru.touch(0, 1, CacheReplacementPolicy::TouchOp::REFILL);
    il1_lru.touch(0, 0, CacheReplacementPolicy::TouchOp::REFILL);
    ASSERT_EQ(3, il1_lru.victim(0));
}

TEST_F(IL1LRUTest, victim_after_victim)
{
    il1_lru.init(1, 4);
    il1_lru.touch(0, 3, CacheReplacementPolicy::TouchOp::REFILL);
    il1_lru.touch(0, 2, CacheReplacementPolicy::TouchOp::REFILL);
    il1_lru.touch(0, 1, CacheReplacementPolicy::TouchOp::REFILL);
    il1_lru.touch(0, 0, CacheReplacementPolicy::TouchOp::REFILL);
    ASSERT_EQ(3, il1_lru.victim(0));
    il1_lru.touch(0, 3, CacheReplacementPolicy::TouchOp::REFILL);
    il1_lru.touch(0, 2, CacheReplacementPolicy::TouchOp::REFILL);
    ASSERT_EQ(1, il1_lru.victim(0));
}

TEST_F(IL1LRUTest, inval_no_update_lru)
{
    il1_lru.init(1, 4);
    il1_lru.touch(0, 3, CacheReplacementPolicy::TouchOp::REFILL);
    il1_lru.touch(0, 2, CacheReplacementPolicy::TouchOp::REFILL);
    il1_lru.touch(0, 1, CacheReplacementPolicy::TouchOp::REFILL);
    il1_lru.touch(0, 0, CacheReplacementPolicy::TouchOp::REFILL);
    ASSERT_EQ(3, il1_lru.victim(0));
    il1_lru.invalidate(0, 0);
    ASSERT_EQ(3, il1_lru.victim(0));
}

} // namespace ampere::uarch_common
