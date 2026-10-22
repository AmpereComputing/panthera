//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <unordered_set>

#include <gtest/gtest.h>

#include "ampere/archx/el.h"
#include "ampere/core/branch_context.h"
#include "ampere/util/bits.h"

using namespace ampere::archx;
using namespace ampere::core;
using json = nlohmann::json;

TEST(testBranchContext, testTokenConstruction)
{
    auto generator = TokenGenerator(71);
    BranchContext ctx1 = {EL0, 15U, generator.next()};
    ASSERT_EQ(ctx1.el, EL0);
    ASSERT_EQ(ctx1.asid, 15U);
}

TEST(testBranchContext, testTokenGenerator)
{
    auto generator = TokenGenerator(53);
    std::unordered_set<uint64_t> past_tokens;
    Token token;
    uint64_t set_bits{0}, unset_bits{0};

    // Generate 50, ensure they are all unique and that every bit in the size
    // is set at least once and unset at least once
    for (unsigned i = 0; i < 50; i++) {
        token = generator.next();
        auto [it, inserted] = past_tokens.emplace(token.bits(53));
        ASSERT_TRUE(inserted);

        uint64_t value = token.bits(53);
        set_bits |= value;
        unset_bits |= ~value;
    }

    uint64_t mask = ampere::util::mask(53);
    ASSERT_EQ(set_bits, mask);
    ASSERT_EQ(unset_bits & mask, mask);
}

TEST(testBranchContext, testTokenBits)
{
    auto generator = TokenGenerator(71);
    BranchContext ctx1 = {EL0, 15U, generator.next()};
    ASSERT_EQ(ctx1.el, EL0);
    ASSERT_EQ(ctx1.asid, 15U);

    // This 0x6ac1f425ff4780eb value is random and empirically determined, but
    // should remain constant since we are seeding the pseudo-random generation
    // consistently
    ASSERT_EQ(ctx1.token.bits(64, 0), 0x6ac1f425ff4780eb);
    ASSERT_EQ(ctx1.token.bits(7, 64), 0x5c);

    // overlapping the 64-bit boundary combines values successfully
    ASSERT_EQ(ctx1.token.bits(23, 48), 0x5c6ac1);

    // requesting only low bits masks off higher ones
    ASSERT_EQ(ctx1.token.bits(7), 0x6b);

    // requesting the middle gets you just that
    ASSERT_EQ(ctx1.token.bits(8, 16), 0x47);

    // calling Token.bits() with out-of-range bits results in an exception
    ASSERT_NO_THROW(ctx1.token.bits(1, 70));
    ASSERT_ANY_THROW(ctx1.token.bits(2, 70));
    ASSERT_ANY_THROW(ctx1.token.bits(22, 60));
    ASSERT_ANY_THROW(ctx1.token.bits(1, 71));
    ASSERT_ANY_THROW(ctx1.token.bits(5, 71));
    ASSERT_ANY_THROW(ctx1.token.bits(65, 0));
}

TEST(testBranchContext, testBranchContextJSON)
{
    auto generator = TokenGenerator(71);
    BranchContext context1 = {EL::EL0, 739, generator.next()};

    json j = context1;

    // This 0x6ac1f425ff4780eb value is random and empirically determined, but
    // should remain constant since we are seeding the pseudo-random generation
    // consistently
    ASSERT_EQ(j.at("el").get<EL>(), EL::EL0);
    ASSERT_EQ(j.at("asid").get<unsigned>(), 739);
    ASSERT_EQ(j.at("token").at("bit_width").get<unsigned>(), 71);
    ASSERT_EQ(j.at("token").at("value").get<std::string>(), "0x5c6ac1f425ff4780eb");

    // context2 starts out as narrower token with different values...
    auto generator2 = TokenGenerator(5);
    BranchContext context2 = {EL::EL1, 334, generator2.next()};
    ASSERT_EQ(context2.el, EL::EL1);
    ASSERT_EQ(context2.asid, 334);
    ASSERT_EQ(context2.token.width(), 5);
    ASSERT_EQ(context2.token.bits(5), 0xd);

    //...but is restored to old, wider, value from JSON (originally from context1)
    context2 = j;
    ASSERT_EQ(context2.el, EL::EL0);
    ASSERT_EQ(context2.asid, 739);
    ASSERT_EQ(context2.token.width(), 71);
    ASSERT_EQ(context2.token.bits(64, 0), 0x6ac1f425ff4780eb);
    ASSERT_EQ(context2.token.bits(7, 64), 0x5c);
}

TEST(testBranchContext, testGenerateZeroToken)
{
    auto generator = TokenGenerator(71);
    BranchContext ctx1 = {EL0, 15U, generator.next(true /*zero*/)};
    ASSERT_EQ(ctx1.el, EL0);
    ASSERT_EQ(ctx1.asid, 15U);
    ASSERT_EQ(ctx1.token.bits(64, 0), 0UL);
    ASSERT_EQ(ctx1.token.bits(29, 23), 0UL);
    ASSERT_EQ(ctx1.token.bits(2, 2), 0UL);
}
