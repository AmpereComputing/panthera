//
// Copyright (c) 2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <gtest/gtest.h>
#include "ampere/sc/testing/test_module.h"
#include "ampere/uarch_common/crp/plru6.h"

using namespace ampere::sc;

namespace ampere::uarch_common {

class PLRU6Test : public TestModule
{
protected:
    PLRU6 plru6{"plru6"};
};

TEST_F(PLRU6Test, init)
{
    plru6.init(1, 6);
    ASSERT_EQ(0, plru6.victim(0));
}

TEST_F(PLRU6Test, untouched_victim)
{
    plru6.init(1, 6);
    ASSERT_EQ(0, plru6.victim(0));
}

TEST_F(PLRU6Test, oldest_touched_victim)
{
    plru6.init(1, 6);
    plru6.touch(0, 0, CacheReplacementPolicy::TouchOp::REFILL);
    plru6.touch(0, 2, CacheReplacementPolicy::TouchOp::REFILL);
    plru6.touch(0, 3, CacheReplacementPolicy::TouchOp::REFILL);
    plru6.touch(0, 1, CacheReplacementPolicy::TouchOp::REFILL);
    plru6.touch(0, 4, CacheReplacementPolicy::TouchOp::REFILL);
    plru6.touch(0, 4, CacheReplacementPolicy::TouchOp::REFILL);
    plru6.touch(0, 2, CacheReplacementPolicy::TouchOp::REFILL);
    plru6.touch(0, 5, CacheReplacementPolicy::TouchOp::REFILL);
    plru6.touch(0, 5, CacheReplacementPolicy::TouchOp::REFILL);
    plru6.touch(0, 1, CacheReplacementPolicy::TouchOp::REFILL);
    plru6.touch(0, 2, CacheReplacementPolicy::TouchOp::REFILL);
    plru6.touch(0, 2, CacheReplacementPolicy::TouchOp::REFILL);
    plru6.touch(0, 3, CacheReplacementPolicy::TouchOp::REFILL);
    plru6.touch(0, 1, CacheReplacementPolicy::TouchOp::REFILL);
    plru6.touch(0, 4, CacheReplacementPolicy::TouchOp::REFILL);
    plru6.touch(0, 0, CacheReplacementPolicy::TouchOp::REFILL);
    plru6.touch(0, 1, CacheReplacementPolicy::TouchOp::REFILL);
    plru6.touch(0, 2, CacheReplacementPolicy::TouchOp::REFILL);
    plru6.touch(0, 1, CacheReplacementPolicy::TouchOp::REFILL);
    plru6.touch(0, 3, CacheReplacementPolicy::TouchOp::REFILL);
    plru6.touch(0, 1, CacheReplacementPolicy::TouchOp::REFILL);
    ASSERT_EQ(5, plru6.victim(0));
}

TEST_F(PLRU6Test, victim_after_victim)
{
    plru6.init(1, 6);
    plru6.touch(0, 0, CacheReplacementPolicy::TouchOp::REFILL);
    ASSERT_EQ(2, plru6.victim(0));
    plru6.touch(0, 2, CacheReplacementPolicy::TouchOp::REFILL);
    ASSERT_EQ(4, plru6.victim(0));
    plru6.touch(0, 3, CacheReplacementPolicy::TouchOp::REFILL);
    ASSERT_EQ(4, plru6.victim(0));
    plru6.touch(0, 1, CacheReplacementPolicy::TouchOp::REFILL);
    ASSERT_EQ(4, plru6.victim(0));
    plru6.touch(0, 4, CacheReplacementPolicy::TouchOp::REFILL);
    ASSERT_EQ(0, plru6.victim(0));
    plru6.touch(0, 4, CacheReplacementPolicy::TouchOp::REFILL);
    ASSERT_EQ(0, plru6.victim(0));
    plru6.touch(0, 2, CacheReplacementPolicy::TouchOp::REFILL);
    ASSERT_EQ(0, plru6.victim(0));
    plru6.touch(0, 5, CacheReplacementPolicy::TouchOp::REFILL);
    ASSERT_EQ(0, plru6.victim(0));
    plru6.touch(0, 5, CacheReplacementPolicy::TouchOp::REFILL);
    ASSERT_EQ(0, plru6.victim(0));
    plru6.touch(0, 1, CacheReplacementPolicy::TouchOp::REFILL);
    ASSERT_EQ(3, plru6.victim(0));
    plru6.touch(0, 2, CacheReplacementPolicy::TouchOp::REFILL);
    ASSERT_EQ(4, plru6.victim(0));
    plru6.touch(0, 2, CacheReplacementPolicy::TouchOp::REFILL);
    ASSERT_EQ(4, plru6.victim(0));
    plru6.touch(0, 3, CacheReplacementPolicy::TouchOp::REFILL);
    ASSERT_EQ(4, plru6.victim(0));
    plru6.touch(0, 1, CacheReplacementPolicy::TouchOp::REFILL);
    ASSERT_EQ(4, plru6.victim(0));
    plru6.touch(0, 4, CacheReplacementPolicy::TouchOp::REFILL);
    ASSERT_EQ(0, plru6.victim(0));
    plru6.touch(0, 0, CacheReplacementPolicy::TouchOp::REFILL);
    ASSERT_EQ(2, plru6.victim(0));
    plru6.touch(0, 1, CacheReplacementPolicy::TouchOp::REFILL);
    ASSERT_EQ(2, plru6.victim(0));
    plru6.touch(0, 2, CacheReplacementPolicy::TouchOp::REFILL);
    ASSERT_EQ(5, plru6.victim(0));
    plru6.touch(0, 1, CacheReplacementPolicy::TouchOp::REFILL);
    ASSERT_EQ(5, plru6.victim(0));
    plru6.touch(0, 3, CacheReplacementPolicy::TouchOp::REFILL);
    ASSERT_EQ(5, plru6.victim(0));
    plru6.touch(0, 1, CacheReplacementPolicy::TouchOp::REFILL);
    ASSERT_EQ(5, plru6.victim(0));
}

} // namespace ampere::uarch_common
