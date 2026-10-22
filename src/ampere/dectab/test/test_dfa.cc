//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
//
// Copyright (C) 2018, Ampere Computing
//
#include "dfa.h"

#include <gtest/gtest.h>

#include "builder.h"

using namespace ampere::dectab;

class DfaTest : public ::testing::Test
{
protected:
    Builder builder;
    DFA dfa{builder};

    RegexNode* empty() { return builder.empty(); }
    RegexNode* fail() { return builder.fail(); }
    RegexNode* zero() { return builder.zero(); }
    RegexNode* one() { return builder.one(); }
    RegexNode* unknown() { return builder.or_(one(), zero()); }
    RegexNode* cat(RegexNode* l, RegexNode* r) { return builder.cat(l, r); }
    RegexNode* excl(RegexNode* l, RegexNode* r) { return builder.excl(l, r); }
    RegexNode* or_(RegexNode* l, RegexNode* r) { return builder.or_(l, r); }
    RegexNode* label(RegexNode* l, int n) { return builder.label(l, n); }
    RegexNode* label(RegexNode* l, int n, const std::string& file, int line)
    {
        auto re = builder.label(l, n);
        builder.set_label_location(n, file, line);
        return re;
    }
};

TEST_F(DfaTest, DeriveEmptyYieldsFail)
{
    EXPECT_EQ(dfa.derive(empty(), '0'), fail());
}

TEST_F(DfaTest, DeriveSymbolFromMatchingSymbolYieldsEmpty)
{
    EXPECT_EQ(dfa.derive(zero(), '0'), empty());
}

TEST_F(DfaTest, DeriveSymbolFromMismatchingSymbolYieldsFail)
{
    EXPECT_EQ(dfa.derive(one(), '0'), fail());
}

TEST_F(DfaTest, DeriveLeftSideOfCatYieldsRightSide)
{
    EXPECT_EQ(dfa.derive(cat(one(), zero()), '1'), zero());
}

TEST_F(DfaTest, DeriveLeftSideOfCatWithMismatchYieldsFail)
{
    EXPECT_EQ(dfa.derive(cat(one(), zero()), '0'), fail());
}

TEST_F(DfaTest, DeriveCatWithEmptyLeftYieldsRightDerivative)
{
    EXPECT_EQ(dfa.derive(cat(empty(), cat(one(), zero())), '1'), zero());
}

TEST_F(DfaTest, DeriveMatchingExclusionFails)
{
    EXPECT_EQ(dfa.derive(excl(one(), one()), '1'), fail());
}

TEST_F(DfaTest, DeriveNotMatchingExclusionSucceeds)
{
    EXPECT_EQ(dfa.derive(excl(one(), zero()), '1'), empty());
}

TEST_F(DfaTest, DeriveOfExclusionYieldsShorterExclusionOrNoExclusion)
{
    auto tree = excl(cat(unknown(), unknown()), cat(zero(), one())); // xx!01
    EXPECT_EQ(dfa.derive(tree, '0'), excl(unknown(), one()));
    EXPECT_EQ(dfa.derive(tree, '1'), unknown());
}

TEST_F(DfaTest, DeriveOrYieldsOrOfDerivatives)
{
    EXPECT_EQ(dfa.derive(or_(cat(one(), zero()), cat(one(), one())), '1'), or_(zero(), one()));
}

TEST_F(DfaTest, DeriveOfLabelYieldsLabelOfDerivative)
{
    EXPECT_EQ(dfa.derive(label(cat(one(), zero()), 1), '1'), label(zero(), 1));
}

TEST_F(DfaTest, BuildOfEmptyYieldsOneState)
{
    auto m = dfa.build(empty());
    EXPECT_EQ(m->inner_state, 0);
    EXPECT_EQ(m->is_accepting, true);
}

TEST_F(DfaTest, BuildOf0YieldsTwoStateMachine)
{
    auto m = dfa.build(zero());
    ASSERT_EQ(m->inner_edges.size(), 1);
    auto next_state = std::get<0>(m->inner_edges.front());
    EXPECT_EQ(std::get<1>(m->inner_edges.front()), '0');
    EXPECT_FALSE(m->is_accepting);
    EXPECT_TRUE(next_state->is_accepting);
    EXPECT_NE(m->inner_state, -1);
    EXPECT_NE(next_state->inner_state, -1);
}

TEST_F(DfaTest, BuildOf01YieldsThreeStateMachine)
{
    auto m = dfa.build(cat(zero(), one()));
    ASSERT_EQ(m->inner_edges.size(), 1);
    auto next_state = std::get<0>(m->inner_edges.front());
    ASSERT_EQ(next_state->inner_edges.size(), 1);
    auto next_next_state = std::get<0>(next_state->inner_edges.front());
    EXPECT_EQ(std::get<1>(next_state->inner_edges.front()), '1');
    EXPECT_FALSE(m->is_accepting);
    EXPECT_FALSE(next_state->is_accepting);
    EXPECT_TRUE(next_next_state->is_accepting);
    EXPECT_NE(m->inner_state, -1);
    EXPECT_NE(next_state->inner_state, -1);
    EXPECT_NE(next_next_state->inner_state, -1);
}

TEST_F(DfaTest, BuildOf0Or1YieldsForkingMachine)
{
    auto m = dfa.build(or_(zero(), one()));
    ASSERT_EQ(m->inner_edges.size(), 2);
    auto edge0 = m->inner_edges[0];
    auto edge1 = m->inner_edges[1];

    EXPECT_EQ(std::get<1>(edge0), '0');
    EXPECT_EQ(std::get<1>(edge1), '1');

    EXPECT_TRUE(std::get<0>(edge0)->is_accepting);
    EXPECT_TRUE(std::get<0>(edge1)->is_accepting);

    EXPECT_NE(std::get<0>(edge0)->inner_state, -1);
    EXPECT_NE(std::get<0>(edge1)->inner_state, -1);
}

TEST_F(DfaTest, LabelGeneratesEdgeLabel)
{
    auto m = dfa.build(or_(label(zero(), 42), label(one(), 101)));
    ASSERT_EQ(m->inner_edges.size(), 2);
    EXPECT_EQ(std::get<2>(m->inner_edges[0]), 42);
    EXPECT_EQ(std::get<2>(m->inner_edges[1]), 101);
}

TEST_F(DfaTest, AmbiguousDecodeIsDetected)
{
    try {
        dfa.build(or_(label(zero(), 10, "foo", 3), label(zero(), 20, "bar", 7)));
    } catch (std::runtime_error& err) {
        EXPECT_STREQ(err.what(), "patterns at foo line 3 and bar line 7 are ambiguous");
    }
}

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
