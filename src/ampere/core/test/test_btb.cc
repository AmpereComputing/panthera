//
// Copyright (c) 2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <gtest/gtest.h>
#include "ampere/core/btb.h"
#include "ampere/sc/testing/test_module.h"

namespace ampere {

using namespace archx;
using namespace core;

class BTBComponent : public BTB
{
public:
    using BTB::BTB;
    using BTB::compute_index;
};

class BTBTest : public TestModule
{
protected:
    explicit BTBTest(size_t bank_bits = 0)
        : TestModule{{"btb.el_tagged", true}}, btb("btb", bank_bits), context{EL::EL0, 0, token_generator.next(true)}
    {
    }

    BTBComponent btb;
    BranchHistory history{"history"};
    TokenGenerator token_generator{71};
    BranchContext context;

    int current_fetch_id{1};
};

TEST_F(BTBTest, PredictFromEmptyBTBDoesNothing)
{
    Fetch fetch = make<Fetch>(0x1000_pc, current_fetch_id++);
    btb.predict(fetch, history, context);

    for (auto slot : fetch->line_prediction) {
        EXPECT_EQ(slot.branch_type, BranchType::NONE);
        EXPECT_FALSE(slot.primary.target.has_value());
    }
}

TEST_F(BTBTest, CanTrainOneBranch)
{
    Fetch fetch1 = make<Fetch>(0x1000_pc, current_fetch_id++);
    Fetch fetch2 = make<Fetch>(0x1000_pc, current_fetch_id++);

    BranchInfo br_info(BranchType::CONDITIONAL_DIRECT, 0x1004_pc, fetch1, 0);
    br_info.taken = true;
    br_info.target = 0x2000_pc;

    btb.train(br_info, history, context);
    btb.predict(fetch2, history, context);

    EXPECT_EQ(fetch2->line_prediction[1].branch_type, BranchType::CONDITIONAL_DIRECT);
    EXPECT_EQ(*fetch2->line_prediction[1].primary.target, 0x2000_pc);

    for (int i = 0; i < 16; i++) {
        if (i == 1)
            continue;

        auto& slot = fetch2->line_prediction[i];

        EXPECT_EQ(slot.branch_type, BranchType::NONE);
        EXPECT_FALSE(slot.primary.target.has_value());
    }
}

TEST_F(BTBTest, CantTrainAcrossEL)
{
    BranchContext context_el0{EL::EL0, 0, context.token};
    BranchContext context_el1{EL::EL1, 0, context.token};

    Fetch fetch1 = make<Fetch>(0x1000_pc, current_fetch_id++);
    Fetch fetch2 = make<Fetch>(0x1000_pc, current_fetch_id++);

    BranchInfo br_info(BranchType::CONDITIONAL_DIRECT, 0x1004_pc, fetch1, 0);
    br_info.taken = true;
    br_info.target = 0x2000_pc;

    btb.train(br_info, history, context_el1);
    btb.predict(fetch2, history, context_el0);

    EXPECT_EQ(fetch2->line_prediction[1].branch_type, BranchType::NONE);
}

TEST_F(BTBTest, SetComputationMatchesFuriosaRTL)
{
    // 11111111111111100011'00110010100'01011001001'001000
    //
    //     00110010100
    // XOR 01011001001
    //     -----------
    //     01101011101
    //
    auto [set, tag] = btb.compute_index(0xfffe3328b248_pc, 0);
    EXPECT_EQ(set, 0b01101011101);
}

class BankedBTBTest : public BTBTest
{
protected:
    BankedBTBTest() : BTBTest(2 /*bank_bits*/) {}
};

TEST_F(BankedBTBTest, SetComputationWithBanks)
{
    // 111111111111111000110011'001010001'011001001'001000
    //
    //     001010001
    // XOR 011001001
    //     -----------
    //     010011000
    // (then add two high bits for indicated bank ID)

    auto [set0, tag0] = btb.compute_index(0xfffe3328b248_pc, 0);
    EXPECT_EQ(set0, 0b00010011000);

    auto [set3, tag3] = btb.compute_index(0xfffe3328b248_pc, 3);
    EXPECT_EQ(set3, 0b11010011000);
}

class BTBTestWithHalfCacheLineBit : public TestModule
{
protected:
    BTBTestWithHalfCacheLineBit() : TestModule{{"btb.use_half_cache_line_bit_in_index", true}} {}

    BTBComponent btb{"btb"};
    BranchHistory history{"history"};

    int current_fetch_id{1};
};

TEST_F(BTBTestWithHalfCacheLineBit, SetComputationMatchesRipleyRTL)
{
    // 111111111111111000110'01100101000'10110010010'01000
    //
    //     01100101000
    // XOR 10110010010
    //     -----------
    //     11010111010
    //
    auto [set, tag] = btb.compute_index(0xfffe3328b248_pc, 0);
    EXPECT_EQ(set, 0b11010111010);
}

} // namespace ampere
