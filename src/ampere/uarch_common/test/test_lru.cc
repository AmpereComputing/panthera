//
// Copyright (c) 2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <gtest/gtest.h>
#include "ampere/sc/testing/test_module.h"
#include "ampere/uarch_common/crp/lru.h"

using namespace ampere::sc;

namespace ampere::uarch_common {

class LRUTest : public TestModule
{
protected:
    LRU lru{"lru"};
};

TEST_F(LRUTest, init)
{
    lru.init(1, 4);
    ASSERT_EQ(0, lru.victim(0));
}

TEST_F(LRUTest, untouched_victim)
{
    lru.init(1, 4);
    ASSERT_EQ(0, lru.victim(0));
}

TEST_F(LRUTest, oldest_touched_victim)
{
    lru.init(1, 4);
    lru.touch(0, 3, CacheReplacementPolicy::TouchOp::REFILL);
    lru.touch(0, 2, CacheReplacementPolicy::TouchOp::REFILL);
    lru.touch(0, 1, CacheReplacementPolicy::TouchOp::REFILL);
    lru.touch(0, 0, CacheReplacementPolicy::TouchOp::REFILL);
    ASSERT_EQ(3, lru.victim(0));
}

TEST_F(LRUTest, victim_after_victim)
{
    lru.init(1, 4);
    lru.touch(0, 3, CacheReplacementPolicy::TouchOp::REFILL);
    lru.touch(0, 2, CacheReplacementPolicy::TouchOp::REFILL);
    lru.touch(0, 1, CacheReplacementPolicy::TouchOp::REFILL);
    lru.touch(0, 0, CacheReplacementPolicy::TouchOp::REFILL);
    ASSERT_EQ(3, lru.victim(0));
    lru.touch(0, 3, CacheReplacementPolicy::TouchOp::REFILL);
    lru.touch(0, 2, CacheReplacementPolicy::TouchOp::REFILL);
    ASSERT_EQ(1, lru.victim(0));
}

} // namespace ampere::uarch_common
