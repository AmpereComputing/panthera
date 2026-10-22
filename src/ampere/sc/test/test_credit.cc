//
// Copyright (c) 2018-2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <gtest/gtest.h>
#include "ampere/sc/credit.h"
#include "ampere/sc/testing/assertions.h"

using ::testing::HasSubstr;

using namespace ampere::sc;

struct CreditPoolTest : public ::testing::Test
{
    void SetUp() override { Environment::get_root()->parameters.clear(); }
};

TEST_F(CreditPoolTest, CanCreateCreditPool)
{
    CreditPool{"pool"};
}

TEST_F(CreditPoolTest, CantAcquireFromEmptyPool)
{
    CreditPool empty_pool{"empty_pool"};
    EXPECT_AMPERE_FATAL(
        empty_pool.can_acquire(),
        HasSubstr("attempt to acquire credit from empty pool empty_pool (probably a missing \"size\" configuration)"));
    EXPECT_AMPERE_FATAL(empty_pool.acquire(), HasSubstr("credit pool exhausted"));
}

TEST_F(CreditPoolTest, CantAcquireFromUndersizedPool)
{
    Environment::get_root()->parameters.add_setting("undersized_pool.size", 1, "unit test");
    CreditPool undersized_pool{"undersized_pool"};
    EXPECT_FALSE(undersized_pool.can_acquire(2));
}

TEST_F(CreditPoolTest, CanAcquireFromSingleCreditPool)
{
    Environment::get_root()->parameters.add_setting("pool.size", 1, "unit test");
    CreditPool pool{"pool"};
    EXPECT_TRUE(pool.all_free());
    ASSERT_TRUE(pool.can_acquire());
}

TEST_F(CreditPoolTest, CanAcquireTwoFromSingleCreditPool)
{
    Environment::get_root()->parameters.add_setting("pool.size", 2, "unit test");
    CreditPool pool{"pool"};
    EXPECT_TRUE(pool.all_free());
    ASSERT_TRUE(pool.can_acquire(2));
}

TEST_F(CreditPoolTest, AcquireAndReleaseSucceeds)
{
    Environment::get_root()->parameters.add_setting("pool.size", 1, "unit test");
    CreditPool pool{"pool"};
    ASSERT_TRUE(pool.can_acquire());
    Credit c = pool.acquire();
    EXPECT_FALSE(pool.all_free());
    c.release();
}

TEST_F(CreditPoolTest, AcquireAndReleaseTwoSucceeds)
{
    Environment::get_root()->parameters.add_setting("pool.size", 2, "unit test");
    CreditPool pool{"pool"};
    ASSERT_TRUE(pool.can_acquire(2));
    Credit c = pool.acquire();
    Credit c2 = pool.acquire();
    EXPECT_FALSE(pool.all_free());
    c.release();
    c2.release();
}

TEST_F(CreditPoolTest, LeakedCreditIsDetected)
{
    Environment::get_root()->parameters.add_setting("pool.size", 1, "unit test");
    CreditPool pool{"pool"};
    EXPECT_DEATH({ pool.acquire(); }, "credit 0 for pool was leaked");
}

TEST_F(CreditPoolTest, CanShrinkPool)
{
    Environment::get_root()->parameters.add_setting("pool.size", 2, "unit test");
    CreditPool pool{"pool"};
    pool.shrink(1);
    ASSERT_EQ(1u, pool.get_count());
}

TEST_F(CreditPoolTest, CountedPool)
{
    Environment::get_root()->parameters.add_setting("pool.size", 5, "unit test");
    CountedCreditPool pool{"pool"};
    pool.set_base_id(1234567891011);
    auto c = pool.acquire();
    auto d = pool.duplicate(c->get_id());
    pool.shrink(2);
    ASSERT_EQ(2u, pool.get_count());
    release_credit(c);
    release_credit(d);
}

TEST_F(CreditPoolTest, CanAcquireNCreditsFromNCreditPool)
{
    Environment::get_root()->parameters.add_setting("pool.size", 4, "unit test");
    CreditPool pool{"pool"};
    std::vector<Credit> credits;

    EXPECT_TRUE(pool.can_acquire(4));

    for (int i = 0; i < 4; i++) {
        EXPECT_EQ(pool.get_occupancy(), static_cast<size_t>(i));
        ASSERT_TRUE(pool.can_acquire());
        credits.emplace_back(pool.acquire());
        EXPECT_FALSE(pool.can_acquire(4 - i));
    }

    EXPECT_FALSE(pool.can_acquire());

    while (!credits.empty()) {
        auto c = std::move(credits.back());
        c->release();
        EXPECT_TRUE(pool.can_acquire());
        credits.pop_back();
    }
}

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
