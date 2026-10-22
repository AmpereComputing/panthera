//
// Copyright (c) 2020-2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <gtest/gtest.h>
#include "ampere/sc/testing/test_module.h"
#include "ampere/uarch_common/crp/rrip.h"

using namespace ampere::sc;

namespace ampere::uarch_common {

class RRIPTest : public TestModule
{
protected:
    RRIP rrip{"rrip"};
};

TEST_F(RRIPTest, init)
{
    rrip.init(1, 4);
    ASSERT_EQ(0, rrip.victim(0));
}

TEST_F(RRIPTest, hit_one)
{
    rrip.init(1, 4);
    rrip.touch(0, 0, CacheReplacementPolicy::TouchOp::LOOKUP);
    ASSERT_EQ(1, rrip.victim(0));
}

TEST_F(RRIPTest, fill_one)
{
    rrip.init(1, 4);
    rrip.touch(0, 0, CacheReplacementPolicy::TouchOp::REFILL);
    ASSERT_EQ(1, rrip.victim(0));
}

TEST_F(RRIPTest, hit_several)
{
    rrip.init(1, 4);
    rrip.touch(0, 0, CacheReplacementPolicy::TouchOp::LOOKUP); // 0 3 3 3
    ASSERT_EQ(1, rrip.victim(0));
    rrip.touch(0, 1, CacheReplacementPolicy::TouchOp::LOOKUP); // 0 0 3 3
    ASSERT_EQ(2, rrip.victim(0));
    rrip.touch(0, 2, CacheReplacementPolicy::TouchOp::LOOKUP); // 0 0 0 3
    ASSERT_EQ(3, rrip.victim(0));
    rrip.touch(0, 3, CacheReplacementPolicy::TouchOp::LOOKUP); // 0 0 0 0
    ASSERT_EQ(0, rrip.victim(0));                              // 3 3 3 3
    ASSERT_EQ(0, rrip.victim(0));
    rrip.touch(0, 1, CacheReplacementPolicy::TouchOp::LOOKUP); // 3 0 3 3
    ASSERT_EQ(0, rrip.victim(0));
    rrip.touch(0, 0, CacheReplacementPolicy::TouchOp::LOOKUP); // 0 0 3 3
    ASSERT_EQ(2, rrip.victim(0));
}

TEST_F(RRIPTest, hit_fill_mix)
{
    rrip.init(1, 4);
    rrip.touch(0, 0, CacheReplacementPolicy::TouchOp::LOOKUP); // 0 3 3 3
    ASSERT_EQ(1, rrip.victim(0));
    rrip.touch(0, 1, CacheReplacementPolicy::TouchOp::REFILL); // 0 2 3 3
    ASSERT_EQ(2, rrip.victim(0));
    rrip.touch(0, 2, CacheReplacementPolicy::TouchOp::LOOKUP); // 0 2 0 3
    ASSERT_EQ(3, rrip.victim(0));
    rrip.touch(0, 3, CacheReplacementPolicy::TouchOp::REFILL); // 0 2 0 2
    ASSERT_EQ(1, rrip.victim(0));                              // 1 3 1 3
    rrip.touch(0, 3, CacheReplacementPolicy::TouchOp::REFILL); // 1 3 1 2
    ASSERT_EQ(1, rrip.victim(0));
    rrip.touch(0, 1, CacheReplacementPolicy::TouchOp::LOOKUP); // 1 0 1 2
    ASSERT_EQ(3, rrip.victim(0));                              // 2 1 2 3
}

} // namespace ampere::uarch_common
