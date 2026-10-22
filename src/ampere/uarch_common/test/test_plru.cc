//
// Copyright (c) 2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <gtest/gtest.h>
#include "ampere/sc/testing/test_module.h"
#include "ampere/uarch_common/crp/plru.h"

using namespace ampere::sc;

namespace ampere::uarch_common {

class PLRUTest : public TestModule
{
protected:
    PLRU plru{"plru"};
};

TEST_F(PLRUTest, init)
{
    plru.init(1, 4);
    ASSERT_EQ(0, plru.victim(0));
}

TEST_F(PLRUTest, untouched_victim)
{
    plru.init(1, 4);
    ASSERT_EQ(0, plru.victim(0));
}

TEST_F(PLRUTest, oldest_touched_victim)
{
    plru.init(1, 4);
    plru.touch(0, 3, CacheReplacementPolicy::TouchOp::REFILL);
    plru.touch(0, 2, CacheReplacementPolicy::TouchOp::REFILL);
    plru.touch(0, 1, CacheReplacementPolicy::TouchOp::REFILL);
    plru.touch(0, 0, CacheReplacementPolicy::TouchOp::REFILL);
    ASSERT_EQ(3, plru.victim(0));
}

TEST_F(PLRUTest, victim_after_victim)
{
    plru.init(1, 4);
    plru.touch(0, 1, CacheReplacementPolicy::TouchOp::REFILL);
    plru.touch(0, 2, CacheReplacementPolicy::TouchOp::REFILL);
    plru.touch(0, 3, CacheReplacementPolicy::TouchOp::REFILL);
    plru.touch(0, 0, CacheReplacementPolicy::TouchOp::REFILL);
    ASSERT_EQ(2, plru.victim(0));
    plru.touch(0, 2, CacheReplacementPolicy::TouchOp::REFILL);
    ASSERT_EQ(1, plru.victim(0));
}

TEST_F(PLRUTest, ignore_nontemporal_lookup)
{
    plru.init(1, 4);
    plru.touch(0, 3, CacheReplacementPolicy::TouchOp::REFILL);
    plru.touch(0, 2, CacheReplacementPolicy::TouchOp::REFILL);
    plru.touch(0, 1, CacheReplacementPolicy::TouchOp::REFILL);
    plru.touch(0, 0, CacheReplacementPolicy::TouchOp::REFILL);
    plru.touch(0, 3, CacheReplacementPolicy::TouchOp::LOOKUP, true);
    ASSERT_EQ(3, plru.victim(0));
}

TEST_F(PLRUTest, vicitmize_newer_nontemporal)
{
    plru.init(1, 4);
    plru.touch(0, 3, CacheReplacementPolicy::TouchOp::REFILL);
    plru.touch(0, 2, CacheReplacementPolicy::TouchOp::REFILL);
    plru.touch(0, 1, CacheReplacementPolicy::TouchOp::REFILL);
    plru.touch(0, 0, CacheReplacementPolicy::TouchOp::REFILL, true);
    ASSERT_EQ(0, plru.victim(0));
}

} // namespace ampere::uarch_common
