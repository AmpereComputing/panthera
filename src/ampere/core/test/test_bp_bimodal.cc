//
// Copyright (c) 2019, 2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <gtest/gtest.h>
#include "ampere/core/bp_bimodal.h"
#include "ampere/core/fetch.h"
#include "ampere/sc/testing/test_module.h"

namespace ampere::bp {

using namespace core;

class BPBimodalTest : public TestModule
{
protected:
    BPBimodalTest() : TestModule{{"bimodal.sets", 1024}}, context{EL::EL0, 0U, token_generator.next(true)} {}

    BPBiModal bimodal{"bimodal", 0};
    BranchHistory history{"history"};
    TokenGenerator token_generator{71};
    BranchContext context;

    Fetch make_fetch(ProgramCounter pc) { return make<Fetch>(pc, current_fetch_id++); }

    void predict(Fetch fetch) { bimodal.predict(fetch, history, context); }

    void train(Fetch fetch, ProgramCounter branch_pc, bool taken)
    {
        BranchInfo br_info{BranchType::CONDITIONAL_DIRECT, branch_pc, fetch, 0};
        br_info.taken = taken;
        bimodal.train(br_info, history, context);
    }

    int current_fetch_id{0};
};

TEST_F(BPBimodalTest, PredictFromEmptyBiModalReturnsNoBranches)
{
    ProgramCounter pc{0x1000};

    auto fetch = make_fetch(pc);
    predict(fetch);

    for (size_t i = 0; i < fetch->line_prediction.size(); i++)
        ASSERT_EQ(fetch->line_prediction[0].branch_type, BranchType::NONE);
}

TEST_F(BPBimodalTest, CounterStartsAsWeaklyTaken)
{
    ProgramCounter pc{0x1000};

    auto fetch = make_fetch(pc);

    train(fetch, pc, true);
    predict(fetch);

    EXPECT_TRUE(fetch->line_prediction[0].primary.taken);
}

TEST_F(BPBimodalTest, CounterStaturatesAtStronglyTaken)
{
    ProgramCounter pc{0x1000};

    auto fetch = make_fetch(pc);

    predict(fetch);
    train(fetch, pc, true);
    train(fetch, pc, true);
    train(fetch, pc, true);
    train(fetch, pc, true);
    train(fetch, pc, true);
    train(fetch, pc, true);

    predict(fetch);
    EXPECT_TRUE(fetch->line_prediction[0].primary.taken);

    train(fetch, pc, false);
    predict(fetch);
    EXPECT_TRUE(fetch->line_prediction[0].primary.taken);

    train(fetch, pc, false);
    predict(fetch);
    EXPECT_FALSE(fetch->line_prediction[0].primary.taken);
}

TEST_F(BPBimodalTest, CounterStaturatesAtStronglyNotTaken)
{
    ProgramCounter pc{0x1000};

    auto fetch = make_fetch(pc);

    predict(fetch);
    train(fetch, pc, false);
    train(fetch, pc, false);
    train(fetch, pc, false);
    train(fetch, pc, false);
    train(fetch, pc, false);

    predict(fetch);
    EXPECT_FALSE(fetch->line_prediction[0].primary.taken);

    train(fetch, pc, true);
    predict(fetch);
    EXPECT_FALSE(fetch->line_prediction[0].primary.taken);

    train(fetch, pc, true);
    predict(fetch);
    EXPECT_TRUE(fetch->line_prediction[0].primary.taken);
}

TEST_F(BPBimodalTest, MultipleTrainsOneOneLine)
{
    auto fetch = make_fetch(0x1000_pc);

    // All slots start out weakly not-taken (N, W). Note that there is 1
    // strength (a.k.a. hysteresis) bit shared between 4 neighboring offsets.
    // offset:    0 1 2 3 4 5 6 7 8 9 A B C D E F
    // direction: N N N N N N N N N N N N N N N N
    // strength:     W   |   W   |   W   |   W
    predict(fetch);
    for (size_t offset = 0; offset < 16; offset++)
        EXPECT_EQ(fetch->line_prediction[offset].primary.taken, false);

    train(fetch, 0x100c_pc, true);
    // Slot 3 becomes weakly taken (T, W):
    // offset:    0 1 2 3 4 5 6 7 8 9 A B C D E F
    // direction: N N N T N N N N N N N N N N N N
    // strength:     W   |   W   |   W   |   W
    predict(fetch);
    for (size_t offset = 0; offset < 16; offset++)
        EXPECT_EQ(fetch->line_prediction[offset].primary.taken, offset == 3);

    train(fetch, 0x103c_pc, true);
    // Slot 15 becomes weakly taken (T, W):
    // offset:    0 1 2 3 4 5 6 7 8 9 A B C D E F
    // direction: N N N T N N N N N N N N N N N T
    // strength:     W   |   W   |   W   |   W
    predict(fetch);
    for (size_t offset = 0; offset < 16; offset++)
        EXPECT_EQ(fetch->line_prediction[offset].primary.taken, offset == 3 || offset == 15);

    train(fetch, 0x100c_pc, false);
    // Slot 3 becomes weakly not-taken again (N, W):
    // offset:    0 1 2 3 4 5 6 7 8 9 A B C D E F
    // direction: N N N N N N N N N N N N N N N T
    // strength:     W   |   W   |   W   |   W
    predict(fetch);
    for (size_t offset = 0; offset < 16; offset++)
        EXPECT_EQ(fetch->line_prediction[offset].primary.taken, offset == 15);

    train(fetch, 0x103c_pc, true);
    // Slot 15 becomes strongly taken (T, W):
    // offset:    0 1 2 3 4 5 6 7 8 9 A B C D E F
    // direction: N N N N N N N N N N N N N N N T
    // strength:     W   |   W   |   W   |   S
    predict(fetch);
    for (size_t offset = 0; offset < 16; offset++)
        EXPECT_EQ(fetch->line_prediction[offset].primary.taken, offset == 15);

    train(fetch, 0x1034_pc, true);
    // Slot 13 does *NOT* become taken, because the strength for its hysteresis
    // cluster is strong, but it does weaken it, so the next training can
    // change it
    // offset:    0 1 2 3 4 5 6 7 8 9 A B C D E F
    // direction: N N N N N N N N N N N N N N N T
    // strength:     W   |   W   |   W   |   W
    predict(fetch);
    for (size_t offset = 0; offset < 16; offset++)
        EXPECT_EQ(fetch->line_prediction[offset].primary.taken, offset == 15);

    train(fetch, 0x1034_pc, true);
    // Slot 13 becomes weakly taken, because the strength for its hysteresis
    // cluster is now weak.
    // offset:    0 1 2 3 4 5 6 7 8 9 A B C D E F
    // direction: N N N N N N N N N N N N N T N T
    // strength:     W   |   W   |   W   |   W

    predict(fetch);
    for (size_t offset = 0; offset < 16; offset++)
        EXPECT_EQ(fetch->line_prediction[offset].primary.taken, offset == 13 || offset == 15);

    train(fetch, 0x103c_pc, false);
    // Slot 15 becomes weakly not-taken (N, W):
    // offset:    0 1 2 3 4 5 6 7 8 9 A B C D E F
    // direction: N N N N N N N N N N N N N T N N
    // strength:     W   |   W   |   W   |   W
    predict(fetch);
    for (size_t offset = 0; offset < 16; offset++)
        EXPECT_EQ(fetch->line_prediction[offset].primary.taken, offset == 13);
}

} // namespace ampere::bp
