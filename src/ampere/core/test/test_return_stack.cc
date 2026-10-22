//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <gtest/gtest.h>
#include "ampere/core/bp_rsb.h"
#include "ampere/sc/testing/test_module.h"

using namespace ampere::archx;
using namespace ampere::core;

class ReturnStackTest : public TestModule
{
protected:
    ReturnStackTest() : TestModule{}, context{EL::EL0, 0, token_generator.next(true)} {}

    ReturnStack rsb{"rsb"};
    TokenGenerator token_generator{71};
    BranchContext context;
};

static Fetch fetch(FetchId id)
{
    static std::map<FetchId, Fetch> fetches;
    auto f = fetches.find(id);

    if (f != fetches.end()) {
        return f->second;
    } else {
        auto fetch = trans_make<Fetch>(0_pc, id);
        fetches[id] = fetch;
        return fetch;
    }
}

Fetch operator""_F(unsigned long long id)
{
    return fetch(id);
}

TEST_F(ReturnStackTest, ReturnFromNewStackPredictsNothing)
{
    EXPECT_FALSE(rsb.spec_ret(0_F, context));
}

TEST_F(ReturnStackTest, PushSpeculativeProvidesNewTopEntry)
{
    rsb.spec_call(0_F, 2000_pc, context);
    EXPECT_EQ(*rsb.spec_ret(1_F, context), 2004_pc);
}

TEST_F(ReturnStackTest, TwoPushesLeavesNewestOnTop)
{
    rsb.spec_call(0_F, 2000_pc, context);
    rsb.spec_call(1_F, 3000_pc, context);
    EXPECT_EQ(*rsb.spec_ret(2_F, context), 3004_pc);
    EXPECT_EQ(*rsb.spec_ret(3_F, context), 2004_pc);
}

TEST_F(ReturnStackTest, CommitToEmptyStackThrows)
{
    ASSERT_ANY_THROW(rsb.commit_call(1_F));
}

TEST_F(ReturnStackTest, ReturnWithNoMatchingCallReturnsNothing)
{
    rsb.spec_ret(0_F, context);
    EXPECT_FALSE(rsb.commit_ret(0_F));
}

TEST_F(ReturnStackTest, CommitCallOfMismatchingFetchIdThrows)
{
    rsb.spec_call(0_F, 2000_pc, context);
    ASSERT_ANY_THROW(rsb.commit_call(1_F));
}

TEST_F(ReturnStackTest, CommitRetInProperOrder)
{
    rsb.spec_call(0_F, 1000_pc, context);
    rsb.spec_call(1_F, 2000_pc, context);
    rsb.spec_call(2_F, 3000_pc, context);
    rsb.spec_ret(3_F, context);
    rsb.spec_ret(4_F, context);
    rsb.spec_ret(5_F, context);
    rsb.commit_call(0_F);
    rsb.commit_call(1_F);
    rsb.commit_call(2_F);
    EXPECT_EQ(rsb.commit_ret(3_F), 3004_pc);
    EXPECT_EQ(rsb.commit_ret(4_F), 2004_pc);
    EXPECT_EQ(rsb.commit_ret(5_F), 1004_pc);
}

TEST_F(ReturnStackTest, CommitFollowsSpeculative)
{
    rsb.spec_call(0_F, 10_pc, context);
    rsb.spec_call(1_F, 20_pc, context);
    EXPECT_EQ(*rsb.spec_ret(2_F, context), 24_pc);
    rsb.spec_call(3_F, 30_pc, context);
    rsb.spec_call(4_F, 40_pc, context);
    EXPECT_EQ(*rsb.spec_ret(5_F, context), 44_pc);
    EXPECT_EQ(*rsb.spec_ret(6_F, context), 34_pc);
    EXPECT_EQ(*rsb.spec_ret(7_F, context), 14_pc);

    rsb.commit_call(0_F);
    rsb.commit_call(1_F);
    EXPECT_EQ(rsb.commit_ret(2_F), 24_pc);
    rsb.commit_call(3_F);
    rsb.commit_call(4_F);
    EXPECT_EQ(rsb.commit_ret(5_F), 44_pc);
    EXPECT_EQ(rsb.commit_ret(6_F), 34_pc);
    EXPECT_EQ(rsb.commit_ret(7_F), 14_pc);
}

TEST_F(ReturnStackTest, CommitFollowsSpeculativeInterwoven)
{
    // Same test as above, but the commits are mixed in with the speculative functions.
    rsb.spec_call(0_F, 10_pc, context);
    rsb.commit_call(0_F);
    rsb.spec_call(1_F, 20_pc, context);
    EXPECT_EQ(*rsb.spec_ret(2_F, context), 24_pc);
    rsb.commit_call(1_F);
    rsb.spec_call(3_F, 30_pc, context);
    EXPECT_EQ(rsb.commit_ret(2_F), 24_pc);
    rsb.commit_call(3_F);
    rsb.spec_call(4_F, 40_pc, context);
    EXPECT_EQ(*rsb.spec_ret(5_F, context), 44_pc);
    rsb.commit_call(4_F);
    EXPECT_EQ(*rsb.spec_ret(6_F, context), 34_pc);
    EXPECT_EQ(rsb.commit_ret(5_F), 44_pc);
    EXPECT_EQ(rsb.commit_ret(6_F), 34_pc);
    EXPECT_EQ(*rsb.spec_ret(7_F, context), 14_pc);
    EXPECT_EQ(rsb.commit_ret(7_F), 14_pc);
}

TEST_F(ReturnStackTest, FlushWillClearSpeculativeState)
{
    rsb.spec_call(0_F, 10_pc, context);
    rsb.spec_call(1_F, 20_pc, context);
    rsb.flush(1);
    EXPECT_EQ(*rsb.spec_ret(2_F, context), 14_pc);
}

TEST_F(ReturnStackTest, FlushOutisdeOfRangeDoesNothing)
{
    rsb.spec_call(0_F, 10_pc, context);
    rsb.spec_call(1_F, 20_pc, context);
    rsb.commit_call(0_F);
    rsb.commit_call(1_F);
    rsb.flush(2);
    EXPECT_EQ(*rsb.spec_ret(2_F, context), 24_pc);
    EXPECT_EQ(*rsb.spec_ret(3_F, context), 14_pc);
    EXPECT_EQ(rsb.commit_ret(2_F), 24_pc);
    EXPECT_EQ(rsb.commit_ret(3_F), 14_pc);
}

TEST_F(ReturnStackTest, FlushOfAllSpeculativeStateWorks)
{
    rsb.spec_call(0_F, 10_pc, context);
    rsb.flush(0);
    rsb.spec_call(1_F, 20_pc, context);
    EXPECT_EQ(*rsb.spec_ret(2_F, context), 24_pc);
}

TEST_F(ReturnStackTest, FlushRestoresSpeculativeTOSProperly)
{
    rsb.spec_call(10_F, 10_pc, context);
    rsb.spec_call(11_F, 20_pc, context);
    rsb.spec_ret(12_F, context); // speculative TOS is now 10/10_pc
    rsb.spec_call(13_F, 30_pc, context);
    rsb.flush(13);
    EXPECT_EQ(*rsb.spec_ret(14_F, context), 14_pc);
}

TEST_F(ReturnStackTest, CheckEL)
{
    rsb.spec_call(0_F, 10_pc, context);
    context.el = EL::EL1;
    EXPECT_EQ(*rsb.spec_ret(1_F, context), 0_pc);
    rsb.flush(1);
    context.el = EL::EL0;
    EXPECT_EQ(*rsb.spec_ret(1_F, context), 14_pc);
}
