//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
//
// Copyright (C) 2018, Ampere Computing
//
#include "builder.h"

#include <gtest/gtest.h>

using namespace ampere::dectab;

TEST(BuilderTest, TwoZerosArePointerEqual)
{
    Builder builder;
    auto z1 = builder.zero();
    auto z2 = builder.make_regex(RegexNode::Op::SYM, '0');
    EXPECT_EQ(z1, z2);
}

TEST(BuilderTest, EquivalentExpressionsArePointerEqual)
{
    Builder builder;
    auto e1 = builder.or_(builder.zero(), builder.one());
    auto e2 = builder.or_(builder.zero(), builder.one());
    auto e3 = builder.cat(builder.one(), builder.cat(builder.one(), builder.one()));
    auto e4 = builder.cat(builder.one(), builder.cat(builder.one(), builder.one()));
    EXPECT_EQ(e1->op, RegexNode::Op::OR);
    EXPECT_EQ(e1, e2);
    EXPECT_EQ(e3->op, RegexNode::Op::CAT);
    EXPECT_EQ(e3, e4);
}

TEST(BuilderTest, CatWithEmptyYieldsOriginalString)
{
    Builder builder;
    EXPECT_EQ(builder.cat(builder.empty(), builder.one()), builder.one());
    EXPECT_EQ(builder.cat(builder.zero(), builder.empty()), builder.zero());
}

TEST(BuilderTest, CatWithFailYieldsFail)
{
    Builder builder;
    EXPECT_EQ(builder.cat(builder.one(), builder.fail()), builder.fail());
    EXPECT_EQ(builder.cat(builder.fail(), builder.zero()), builder.fail());
}

TEST(BuilderTest, OrWithFailYieldsOtherSide)
{
    Builder builder;
    EXPECT_EQ(builder.or_(builder.one(), builder.fail()), builder.one());
    EXPECT_EQ(builder.or_(builder.fail(), builder.one()), builder.one());
    EXPECT_EQ(builder.or_(builder.empty(), builder.fail()), builder.empty());
    EXPECT_EQ(builder.or_(builder.fail(), builder.empty()), builder.empty());
}

TEST(BuilderTest, OrWithTwoEmptiesYieldsEmpty)
{
    Builder builder;
    EXPECT_EQ(builder.or_(builder.empty(), builder.empty()), builder.empty());
}

TEST(BuilderTest, OrderDoesntMatterWithOr)
{
    Builder builder;
    EXPECT_EQ(builder.or_(builder.zero(), builder.one()), builder.or_(builder.one(), builder.zero()));
}

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
