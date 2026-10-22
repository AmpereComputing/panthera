//
// Copyright (c) 2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <gtest/gtest.h>
#include "ampere/archx/instruction_simple.h"
#include "ampere/core/instruction.h"
#include "ampere/core/loop_detector.h"
#include "ampere/sc/testing/test_module.h"

using namespace ampere::core;

class LoopDetectorTest : public TestModule
{
protected:
    LoopDetectorTest() : TestModule{{"ld.iq_size", 32}}, context{EL::EL0, 0U, token_generator.next(true)}
    {
        history.add_history_size(4);
    }

    void SetUp() override { archx::Instruction::set_decoders({"furiosa"}); }

    LoopDetector ld{"ld"};
    BranchHistory history{"history"};
    TokenGenerator token_generator{71};
    BranchContext context;

    Fetch make_fetch(ProgramCounter pc) { return make<Fetch>(pc, current_fetch_id++); }

    int current_fetch_id{0};
};

TEST_F(LoopDetectorTest, FindLoopInOneCacheLine)
{
    Prediction pred{BranchType::CONDITIONAL_DIRECT, 0x1020_pc, 0x1000_pc};

    EXPECT_EQ(ld.get_mode(), LoopDetector::Mode::SEARCHING);

    // Five loops to prime the pump. History is four deep, but we're pushing to it
    // after the prediction (which is "realistic") so it takes an extra time to
    // notice the saturation.
    //
    for (int i = 0; i < 5; i++) {
        auto fetch = make_fetch(0x1000_pc);
        fetch->update_prediction(pred);

        ld.predict(fetch, history, context);
        EXPECT_EQ(ld.get_mode(), i % 2 ? LoopDetector::Mode::SEARCHING : LoopDetector::Mode::LOCKING);
        EXPECT_FALSE(ld.looping());
        history.push(fetch->fetch_id, pred.branch_pc, pred.target);
    }

    auto fetch = make_fetch(0x1000_pc);
    fetch->update_prediction(pred);
    ld.predict(fetch, history, context);
    EXPECT_EQ(ld.get_mode(), LoopDetector::Mode::START_LOOP);
    history.push(fetch->fetch_id, pred.branch_pc, pred.target);

    fetch = make_fetch(0x1000_pc);
    fetch->update_prediction(pred);
    ld.predict(fetch, history, context);
    EXPECT_TRUE(fetch->loop_start);
    EXPECT_TRUE(fetch->loop_end);
    EXPECT_TRUE(ld.looping());
}

/*
 * Because of our "loop unrolling" feature, a loop with fewer instructions must
 * be predicted several times to get enough instructions to enter loop mode.
 */
TEST_F(LoopDetectorTest, FindShortLoopInOneCacheLine)
{
    Prediction pred{BranchType::CONDITIONAL_DIRECT, 0x1004_pc, 0x1000_pc};

    EXPECT_EQ(ld.get_mode(), LoopDetector::Mode::SEARCHING);

    // Five loops to prime the pump. History is four deep, but we're pushing to it
    // after the prediction (which is "realistic") so it takes an extra time to
    // notice the saturation.
    //
    for (int i = 0; i < 14; i++) {
        auto fetch = make_fetch(0x1000_pc);
        fetch->update_prediction(pred);

        ld.predict(fetch, history, context);

        if (i <= 3) {
            // it takes 4 iterations to saturate history, but we stay in
            // LOCKING mode because we don't consider the branches because we
            // haven't seen enough instructions to do so.
            EXPECT_EQ(ld.get_mode(), LoopDetector::Mode::LOCKING);
        } else if (i == 4) {
            EXPECT_EQ(ld.get_mode(), LoopDetector::Mode::SEARCHING);
        } else if (i <= 8) {
            // We enter LOCKING a second time but ignore the next 4 iterations
            // because we haven't seen enough instructions (due to unrolling
            // feature)
            EXPECT_EQ(ld.get_mode(), LoopDetector::Mode::LOCKING);
        } else if (i == 9) {
            // After 8 LOCKING iterations, we begin collecting the loop
            EXPECT_EQ(ld.get_mode(), LoopDetector::Mode::START_LOOP);
        } else if (i <= 12) {
            // collect for a total of 4 (counting START_LOOP iteration above),
            // to fulfill our 8 needed instructions per loop
            EXPECT_EQ(ld.get_mode(), LoopDetector::Mode::COLLECTING_LOOP);
            EXPECT_EQ(fetch->loop_start, i == 10); // first COLLECTING prediction should be marked as such
        } else if (i == 13) {
            // finally, we enter loop mode
            EXPECT_EQ(ld.get_mode(), LoopDetector::Mode::LOOPING);
            EXPECT_TRUE(fetch->loop_end);
            EXPECT_TRUE(ld.looping());
        }

        history.push(fetch->fetch_id, pred.branch_pc, pred.target);
    }
}

TEST_F(LoopDetectorTest, FindLoopInTwoCacheLines)
{
    Prediction pred{BranchType::CONDITIONAL_DIRECT, 0x1044_pc, 0x1000_pc};

    EXPECT_EQ(ld.get_mode(), LoopDetector::Mode::SEARCHING);

    // Five loops to prime the pump. History is four deep, but we're pushing to it
    // after the prediction (which is "realistic") so it takes an extra time to
    // notice the saturation.
    //
    for (int i = 0; i < 5; i++) {
        auto fetch1 = make_fetch(0x1000_pc);
        auto fetch2 = make_fetch(0x1040_pc);
        fetch2->update_prediction(pred);

        ld.predict(fetch1, history, context);
        ld.predict(fetch2, history, context);
        EXPECT_EQ(ld.get_mode(), i % 2 ? LoopDetector::Mode::SEARCHING : LoopDetector::Mode::LOCKING);
        history.push(fetch2->fetch_id, pred.branch_pc, pred.target);
    }

    auto fetch1 = make_fetch(0x1000_pc);
    auto fetch2 = make_fetch(0x1040_pc);
    fetch2->update_prediction(pred);
    ld.predict(fetch1, history, context);
    ld.predict(fetch2, history, context);
    EXPECT_EQ(ld.get_mode(), LoopDetector::Mode::START_LOOP);
    history.push(fetch2->fetch_id, pred.branch_pc, pred.target);

    fetch1 = make_fetch(0x1000_pc);
    fetch1 = make_fetch(0x1040_pc);
    fetch2->update_prediction(pred);
    ld.predict(fetch1, history, context);
    EXPECT_EQ(ld.get_mode(), LoopDetector::Mode::COLLECTING_LOOP);
    ld.predict(fetch2, history, context);
    EXPECT_EQ(ld.get_mode(), LoopDetector::Mode::LOOPING);
    EXPECT_TRUE(fetch1->loop_start);
    EXPECT_TRUE(fetch2->loop_end);
    EXPECT_TRUE(ld.looping());
}

TEST_F(LoopDetectorTest, LoopTooLarge)
{
    Prediction pred{BranchType::CONDITIONAL_DIRECT, 0x1084_pc, 0x1000_pc};

    EXPECT_EQ(ld.get_mode(), LoopDetector::Mode::SEARCHING);

    // Loop 20 times, making sure it never moves past the LOCKING state
    // (alternating between SEARCHING and LOCKING), since this loop takes more
    // than the 32 available IQ entries.
    //
    for (int i = 0; i < 20; i++) {
        auto fetch1 = make_fetch(0x1000_pc);
        auto fetch2 = make_fetch(0x1040_pc);
        auto fetch3 = make_fetch(0x1080_pc);
        fetch3->update_prediction(pred);

        ld.predict(fetch1, history, context);
        ld.predict(fetch2, history, context);
        ld.predict(fetch3, history, context);
        if (i % 2)
            EXPECT_EQ(ld.get_mode(), LoopDetector::Mode::SEARCHING);
        else
            EXPECT_EQ(ld.get_mode(), LoopDetector::Mode::LOCKING);
        history.push(fetch3->fetch_id, pred.branch_pc, pred.target);
    }
}

TEST_F(LoopDetectorTest, RecoveryResetsLooping)
{
    Prediction pred{BranchType::CONDITIONAL_DIRECT, 0x1020_pc, 0x1000_pc};

    // Five loops to prime the pump. History is four deep, but we're pushing to it
    // after the prediction (which is "realistic") so it takes an extra time to
    // notice the saturation.
    //
    for (int i = 0; i < 5; i++) {
        auto fetch = make_fetch(0x1000_pc);
        fetch->update_prediction(pred);

        ld.predict(fetch, history, context);
        history.push(fetch->fetch_id, pred.branch_pc, pred.target);
    }

    auto fetch = make_fetch(0x1000_pc);
    fetch->update_prediction(pred);
    ld.predict(fetch, history, context);
    history.push(fetch->fetch_id, pred.branch_pc, pred.target);

    fetch = make_fetch(0x1000_pc);
    fetch->update_prediction(pred);
    ld.predict(fetch, history, context);
    EXPECT_TRUE(ld.looping());

    auto insn = make<ampere::core::Instruction>(std::make_shared<InstructionSimple>(), fetch);
    auto flush = make<Flush>(0x1048_pc, 0x1044_pc, fetch, insn, 1, FlushType::BR_MISPREDICT);
    // really cheating here with null br_info, but the loop detector doesn't look at it
    ld.recover(flush, nullptr, history, context);

    EXPECT_FALSE(ld.looping());
    EXPECT_EQ(ld.get_mode(), LoopDetector::Mode::SEARCHING);
}

class AdvancedLoopDetectorTest : public TestModule
{
protected:
    AdvancedLoopDetectorTest()
        : TestModule{{"ld.iq_size", 32}, {"ld.loop_branch_only_direct", false}},
          context{EL::EL0, 0U, token_generator.next(true)}
    {
        history.add_history_size(4);
    }

    void SetUp() override { archx::Instruction::set_decoders({"furiosa"}); }

    LoopDetector ld{"ld"};
    BranchHistory history{"history"};
    TokenGenerator token_generator{71};
    BranchContext context;

    Fetch make_fetch(ProgramCounter pc) { return make<Fetch>(pc, current_fetch_id++); }

    int current_fetch_id{0};
};

TEST_F(AdvancedLoopDetectorTest, ComplexBranching)
{
    // This is a loop 0x48e458-0x48e478, which calls a short 'function' at
    // 0x43c6e0 inside the loop. The function has a pair of additional (taken)
    // conditional branches. Note: This test doesn't actually use calls/returns
    // because they break out of looping.
    Prediction pred1{BranchType::UNCONDITIONAL_INDIRECT, 0x48e470_pc, 0x43c6e0_pc}; // 0x48e470: blr -> 0x43c6e0
    Prediction pred3{BranchType::CONDITIONAL_DIRECT, 0x43c704_pc, 0x43c710_pc};     // 0x43c704: b.eq -> 0x43c710
    Prediction pred4{BranchType::CONDITIONAL_DIRECT, 0x43c724_pc, 0x43c754_pc};     // 0x43c724: b.le -> 0x43c754
    Prediction pred5{BranchType::UNCONDITIONAL_INDIRECT, 0x43c760_pc, 0x48e474_pc}; // 0x43c760: ret -> 0x48e474
    Prediction pred6{BranchType::CONDITIONAL_DIRECT, 0x48e478_pc, 0x48e458_pc};     // 0x48e478: b.ne -> 0x48e458

    // The first time around the loop we begin LOCKING on the first branch we
    // see (pred1). Because the history is not yet saturated by the time we see
    // it again, the loop detector abandons that branch the very next time it
    // is seen (at the beginning of the 2nd iteration) and goes into LOCKING
    // mode with the next branch it sees (pred3). The branch history is
    // saturated by the time pred3's branch is seen again, so the loop is
    // collected and loop mode is begun.
    //
    // Here is a table of the state of the loop detector *after* the listed
    // fetch is seen in each iteration:
    // iter      | pred 1 & 2         | pred 3             | pred 4, 5, 6
    // ----------|--------------------|--------------------|-------------------
    // 0         | LOCKING (on pred1) | LOCKING (on pred1) | LOCKING (on pred1)
    // 1         | SEARCHING          | LOCKING (on pred3) | LOCKING (on pred3)
    // 2         | LOCKING (on pred3) | START_LOOP         | COLLECTING_LOOP
    // post-loop | COLLECTING_LOOP    | LOOPING

    EXPECT_EQ(ld.get_mode(), LoopDetector::Mode::SEARCHING);
    for (int i = 0; i < 3; i++) {
        auto fetch1 = make_fetch(0x48e458_pc);
        fetch1->update_prediction(pred1);

        auto fetch2 = make_fetch(0x43c6e0_pc);
        auto fetch3 = make_fetch(0x43c700_pc);
        fetch3->update_prediction(pred3);

        auto fetch4 = make_fetch(0x43c710_pc);
        fetch4->update_prediction(pred4);

        auto fetch5 = make_fetch(0x43c754_pc);
        fetch5->update_prediction(pred5);

        auto fetch6 = make_fetch(0x48e474_pc);
        fetch6->update_prediction(pred6);

        ld.predict(fetch1, history, context);
        EXPECT_EQ(ld.get_mode(), i % 2 ? LoopDetector::Mode::SEARCHING : LoopDetector::Mode::LOCKING);
        history.push(fetch1->fetch_id, pred1.branch_pc, pred1.target);

        ld.predict(fetch2, history, context);
        EXPECT_EQ(ld.get_mode(), i % 2 ? LoopDetector::Mode::SEARCHING : LoopDetector::Mode::LOCKING);

        // fetch3 triggers loop collection the third time around
        ld.predict(fetch3, history, context);
        EXPECT_EQ(ld.get_mode(), i == 2 ? LoopDetector::Mode::START_LOOP : LoopDetector::Mode::LOCKING);
        history.push(fetch3->fetch_id, pred3.branch_pc, pred3.target);

        ld.predict(fetch4, history, context);
        if (i == 2) {
            EXPECT_TRUE(fetch4->loop_start);
            EXPECT_EQ(ld.get_mode(), LoopDetector::Mode::COLLECTING_LOOP);
        } else {
            EXPECT_EQ(ld.get_mode(), LoopDetector::Mode::LOCKING);
        }
        history.push(fetch4->fetch_id, pred4.branch_pc, pred4.target);

        ld.predict(fetch5, history, context);
        EXPECT_EQ(ld.get_mode(), i == 2 ? LoopDetector::Mode::COLLECTING_LOOP : LoopDetector::Mode::LOCKING);
        history.push(fetch5->fetch_id, pred5.branch_pc, pred5.target);

        ld.predict(fetch6, history, context);
        EXPECT_EQ(ld.get_mode(), i == 2 ? LoopDetector::Mode::COLLECTING_LOOP : LoopDetector::Mode::LOCKING);
        history.push(fetch6->fetch_id, pred6.branch_pc, pred6.target);
    }

    // Part-way through the 4th time through the loop we start looping
    auto fetch1 = make_fetch(0x48e458_pc);
    fetch1->update_prediction(pred1);
    auto fetch2 = make_fetch(0x43c6e0_pc);
    auto fetch3 = make_fetch(0x43c700_pc);
    fetch3->update_prediction(pred3);

    ld.predict(fetch1, history, context);
    history.push(fetch1->fetch_id, pred1.branch_pc, pred1.target);
    ld.predict(fetch2, history, context);
    EXPECT_EQ(ld.get_mode(), LoopDetector::Mode::COLLECTING_LOOP);
    ld.predict(fetch3, history, context);
    history.push(fetch3->fetch_id, pred3.branch_pc, pred3.target);

    EXPECT_TRUE(fetch3->loop_end);
    EXPECT_TRUE(ld.looping());
}
