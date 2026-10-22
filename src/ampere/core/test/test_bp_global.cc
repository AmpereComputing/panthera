//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <gtest/gtest.h>
#include "ampere/archx/instruction_simple.h"
#include "ampere/core/bob_entry.h"
#include "ampere/core/bp_global.h"
#include "ampere/sc/testing/test_module.h"

namespace ampere::bp {

using namespace archx;
using namespace core;

class BPGlobalTest : public TestModule
{
protected:
    BPGlobalTest()
        : TestModule{{"global.ways", 2}, {"global.ium_entries", 1}, {"global.el_tagged", true}},
          context{EL::EL0, 0, token_generator.next(true)}
    {
        arrays.resize(1);
        arrays[0].resize(1024);
        for (auto& set : arrays[0]) {
            set.resize(2);
            set[1].tag = ~set[1].tag & util::mask(8);
            set[1].most_recently_allocated = true;
        }
        RotationSelect rotation_function = [](Fetch, ProgramCounter, BranchHistory&) {
            return 0;
        };
        global.set_rotation_select(rotation_function);
        global.set_use_alternate_counter(std::make_shared<UseAlternateCounter>());
    }

    void SetUp() override
    {
        history.add_history_size(5);
        history.flush(0);
        history.push(0, 0x1000_pc);
        history.push(1, 0x1040_pc);
        history.push(2, 0x1080_pc);
        history.push(3, 0x10c0_pc);
        history.push(4, 0x1100_pc);
        current_fetch_id = 5;
        archx::Instruction::set_decoders({"furiosa"});
    }

    std::vector<BPGlobalArray> arrays;
    BPGlobal global{"global", arrays, 10, 8, 0};
    BranchHistory history{"history"};
    TokenGenerator token_generator{71};
    BranchContext context;

    Fetch make_fetch(ProgramCounter pc) { return make<Fetch>(pc, current_fetch_id++); }

    BranchInfo make_br_info(Fetch fetch, bool taken)
    {
        auto br_info = BranchInfo{BranchType::CONDITIONAL_DIRECT, fetch->program_counter, fetch, current_alloc_id++};
        br_info.taken = taken;
        return br_info;
    }

    core::Uop create_branch_uop(Fetch fetch, ProgramCounter branch_pc, ProgramCounter target)
    {
        fetch->update_prediction({BranchType::CONDITIONAL_DIRECT, branch_pc, target});
        auto archx_instruction = std::make_shared<archx::InstructionSimple>(branch_pc, 0x14000006);
        archx_instruction->set_next_pc(target);
        auto archx_uops = furiosa::decode(archx_instruction->opcode());
        AMPERE_ASSERT(archx_uops.size() == 1, "unexpected decode result");
        auto archx_uop = std::move(archx_uops[0]);
        auto instruction = trans_make<core::Instruction>(archx_instruction, fetch);
        archx_uop->end_inst = 1;
        auto uop = trans_make<core::Uop>(std::move(archx_uop), instruction);
        uop->alloc_id = 0;
        return uop;
    }

    int current_fetch_id{0};
    std::uint64_t current_alloc_id{0};
};

TEST_F(BPGlobalTest, EmptyGlobalReturnsInvalidPrediction)
{
    auto fetch = make_fetch(0x1000_pc);
    global.predict(fetch, history, context);
    for (size_t i = 0; i < fetch->line_prediction.size(); i++)
        EXPECT_FALSE(fetch->line_prediction[i].primary.taken);
}

TEST_F(BPGlobalTest, CanAllocateThenPredict)
{
    auto fetch = make_fetch(0x1140_pc);

    global.allocate(make_br_info(fetch, true), history, context);

    global.predict(fetch, history, context);

    EXPECT_EQ(fetch->line_prediction[0].primary.predictor, &global);
    EXPECT_TRUE(fetch->line_prediction[0].primary.taken);
}

TEST_F(BPGlobalTest, CannotAllocateThenPredictDifferentEL)
{
    BranchContext context_el0{EL::EL0, 0, context.token};
    BranchContext context_el1{EL::EL1, 0, context.token};

    auto fetch = make_fetch(0x1140_pc);

    global.allocate(make_br_info(fetch, true), history, context_el0);

    global.predict(fetch, history, context_el1);

    EXPECT_NE(fetch->line_prediction[0].primary.predictor, &global);
    EXPECT_FALSE(fetch->line_prediction[0].primary.taken);
}

TEST_F(BPGlobalTest, PredictionCounterStartsAtWeakCorrect)
{
    auto fetch1 = make_fetch(0x4341140_pc);
    auto fetch2 = make_fetch(0x4341240_pc);

    global.allocate(make_br_info(fetch1, true), history, context);

    fetch1->line_prediction[0].primary.taken = true;
    global.train(make_br_info(fetch1, false), history, context); // prediction -> weak not taken

    global.allocate(make_br_info(fetch2, false), history, context);

    fetch2->line_prediction[0].primary.taken = false;
    global.train(make_br_info(fetch2, true), history, context); // prediction -> weak taken

    global.predict(fetch1, history, context);
    EXPECT_FALSE(fetch1->line_prediction[0].primary.taken);

    global.predict(fetch2, history, context);
    EXPECT_TRUE(fetch2->line_prediction[0].primary.taken);
}

TEST_F(BPGlobalTest, CanSaturatePredictionCounter)
{
    auto fetch = make_fetch(0x40bc80_pc);
    BranchInfo br_info = make_br_info(fetch, true);

    // Just make the targets agree so they don't interfere with our test. A conditional global
    // will not set the target, it assumes the BTB has already done that.
    br_info.target = 0x402140_pc;
    br_info.slot.primary.target = 0x402140_pc;

    // prediction starts at T/00 (weak taken)
    global.allocate(br_info, history, context);
    global.predict(fetch, history, context);

    global.train(br_info, history, context); // T/01
    global.train(br_info, history, context); // T/10
    global.train(br_info, history, context); // T/11
    global.train(br_info, history, context); // T/11

    global.predict(fetch, history, context);
    EXPECT_TRUE(fetch->line_prediction[0].primary.taken);
    EXPECT_EQ(fetch->line_prediction[0].primary.strength, BranchStrength::VERYSTRONG);

    br_info.taken = false;
    global.train(br_info, history, context); // T/10
    global.train(br_info, history, context); // T/01
    global.train(br_info, history, context); // T/00

    global.predict(fetch, history, context);
    EXPECT_TRUE(fetch->line_prediction[0].primary.taken);
    EXPECT_EQ(fetch->line_prediction[0].primary.strength, BranchStrength::VERYWEAK);

    global.train(br_info, history, context); // N/00 (weak not taken)

    global.predict(fetch, history, context);
    EXPECT_FALSE(fetch->line_prediction[0].primary.taken);
    EXPECT_EQ(fetch->line_prediction[0].primary.strength, BranchStrength::VERYWEAK);

    global.train(br_info, history, context); // N/01
    global.train(br_info, history, context); // N/10
    global.train(br_info, history, context); // N/11
    global.train(br_info, history, context); // N/11

    br_info.taken = true;

    global.train(br_info, history, context); // N/10
    global.train(br_info, history, context); // N/01
    global.train(br_info, history, context); // N/00

    global.predict(fetch, history, context);
    EXPECT_FALSE(fetch->line_prediction[0].primary.taken);
    EXPECT_EQ(fetch->line_prediction[0].primary.strength, BranchStrength::VERYWEAK);

    global.train(br_info, history, context); // T/00

    global.predict(fetch, history, context);
    EXPECT_TRUE(fetch->line_prediction[0].primary.taken);
    EXPECT_EQ(fetch->line_prediction[0].primary.strength, BranchStrength::VERYWEAK);
}

TEST_F(BPGlobalTest, IUMUpdatesOnMisprediction)
{
    auto fetch = make_fetch(0x401000_pc);
    global.allocate(make_br_info(fetch, false), history, context);
    global.predict(fetch, history, context);

    EXPECT_FALSE(fetch->line_prediction[0].primary.taken);

    auto br_uop = create_branch_uop(fetch, 0x401000_pc, 0x401000_pc);
    BOBEntry bob_entry{br_uop, context};
    bob_entry.br_info.taken = true;
    bob_entry.br_info.mispredicted_direction = true;

    global.recover(make<Flush>(br_uop, FlushType::BR_MISPREDICT), &bob_entry.br_info, history, context);

    // Next prediction from recovery point should be correct, even through we haven't trained yet
    auto fetch2 = make_fetch(0x401000_pc);

    global.predict(fetch2, history, context);
    EXPECT_TRUE(fetch2->line_prediction[0].primary.taken);
    EXPECT_TRUE(fetch2->line_prediction[0].primary.ium);
}

TEST_F(BPGlobalTest, IUMEntryClearsAfterTrain)
{
    auto fetch = make_fetch(0x401000_pc);
    global.allocate(make_br_info(fetch, false), history, context);
    global.predict(fetch, history, context);

    auto br_uop = create_branch_uop(fetch, 0x401000_pc, 0x401000_pc);
    BOBEntry bob_entry{br_uop, context};
    bob_entry.br_info.taken = true;
    bob_entry.br_info.mispredicted_direction = true;

    global.recover(make<Flush>(br_uop, FlushType::BR_MISPREDICT), &bob_entry.br_info, history, context);

    auto fetch2 = make_fetch(0x401000_pc);
    global.predict(fetch2, history, context);

    // Above code creates an IUM entry; now we want to train that branch uop and predict again to show
    // that the prediction is not coming from the IUM
    global.train(bob_entry.br_info, history, context);
    global.clear_ium(0);

    auto fetch3 = make_fetch(0x401000_pc);
    global.predict(fetch3, history, context);

    EXPECT_TRUE(fetch3->line_prediction[0].primary.taken);
    EXPECT_FALSE(fetch3->line_prediction[0].primary.ium);
}

} // namespace ampere::bp
