//
// Copyright (c) 2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include "ampere/archx/instruction_simple.h"
#include "ampere/core/bp_tage.h"
#include "ampere/sc/testing/test_module.h"

namespace ampere::core {

class BPTageTest : public TestModule
{
protected:
    BPTageTest() : TestModule{{"bpu.tage.btb.sets", 1024}}, context{EL::EL0, 0U, token_generator.next(true)} {}

    void SetUp() override { archx::Instruction::set_decoders({"furiosa"}); }

    Fetch make_fetch(ProgramCounter pc) { return make<Fetch>(pc, current_fetch_id++); }

    BP_TAGE tage{"tage"};
    BranchHistory bhr{"bhr"};
    TokenGenerator token_generator{71};
    BranchContext context;

    FetchId current_fetch_id{1};
};

TEST_F(BPTageTest, FirstPredictionIsNoBranch)
{
    auto fetch = make_fetch(0x401000_pc);
    tage.predict(fetch, bhr, context);

    EXPECT_EQ(fetch->get_prediction().branch_type, BranchType::NONE);
}

TEST_F(BPTageTest, TrainAndPredict)
{
    auto fetch = make_fetch(0x401000_pc);
    tage.predict(fetch, bhr, context);

    BranchInfo branch_info{BranchType::CONDITIONAL_DIRECT, 0x401000_pc, fetch, 0};
    branch_info.taken = true;
    tage.train(branch_info, bhr, context);

    auto fetch2 = make_fetch(0x401000_pc);
    tage.predict(fetch2, bhr, context);

    EXPECT_EQ(fetch2->get_prediction().branch_type, BranchType::CONDITIONAL_DIRECT);
}

TEST_F(BPTageTest, BTBWontStoreNotTakenBranch)
{
    auto fetch = make_fetch(0x401000_pc);
    tage.predict(fetch, bhr, context);

    BranchInfo branch_info{BranchType::CONDITIONAL_DIRECT, 0x401000_pc, fetch, 0};
    tage.train(branch_info, bhr, context);

    auto fetch2 = make_fetch(0x401000_pc);
    tage.predict(fetch2, bhr, context);

    EXPECT_EQ(fetch2->get_prediction().branch_type, BranchType::NONE);
}

TEST_F(BPTageTest, BTCWrongTargetWillClearBTBSet)
{
    auto fetch = make_fetch(0x401000_pc);
    tage.predict(fetch, bhr, context);

    BranchInfo branch_info{BranchType::CONDITIONAL_DIRECT, 0x401000_pc, fetch, 0};
    branch_info.taken = true;
    tage.train(branch_info, bhr, context);

    auto fetch2 = make_fetch(0x401000_pc);
    tage.predict(fetch2, bhr, context);

    auto inst = make<Instruction>(std::make_shared<archx::InstructionSimple>(0x401000_pc, 0x54000000), fetch2);
    auto flush = make<Flush>(0x401000_pc, 0x401000_pc, fetch2, inst, 1, FlushType::BRANCH_CHECK_TARGET);

    tage.recover(flush, nullptr, bhr, context);

    auto fetch3 = make_fetch(0x401000_pc);
    tage.predict(fetch3, bhr, context);

    EXPECT_EQ(fetch3->get_prediction().branch_type, BranchType::NONE);
}

TEST_F(BPTageTest, BTCWrongBranchTypeWillClearBTBSet)
{
    auto fetch = make_fetch(0x401000_pc);
    tage.predict(fetch, bhr, context);

    BranchInfo branch_info{BranchType::CONDITIONAL_DIRECT, 0x401000_pc, fetch, 0};
    branch_info.taken = true;
    tage.train(branch_info, bhr, context);

    auto fetch2 = make_fetch(0x401000_pc);
    tage.predict(fetch2, bhr, context);

    auto inst = make<Instruction>(std::make_shared<archx::InstructionSimple>(0x401000_pc, 0x94000001), fetch2);
    auto flush = make<Flush>(0x401000_pc, 0x401000_pc, fetch2, inst, 1, FlushType::BRANCH_CHECK_WRONG_TYPE);

    tage.recover(flush, nullptr, bhr, context);

    auto fetch3 = make_fetch(0x401000_pc);
    tage.predict(fetch3, bhr, context);

    EXPECT_EQ(fetch3->get_prediction().branch_type, BranchType::NONE);
}

} // namespace ampere::core
