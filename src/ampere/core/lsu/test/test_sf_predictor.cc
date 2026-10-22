//
// Copyright (c) 2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <gtest/gtest.h>
#include "ampere/archx/instruction_simple.h"
#include "ampere/core/lsu/sf_predictor.h"
#include "ampere/core/instruction.h"
#include "ampere/sc/testing/test_module.h"

using namespace ampere::sc;
using namespace ampere::archx;
using namespace ampere::core;

namespace ampere::core {

class SFPredictorTest : public TestModule
{
    void SetUp() override { archx::Instruction::set_decoders({"furiosa"}); }

protected:
    SFPredictorTest() : TestModule({{"predictor.sets", 512}, {"predictor.ways", 1}, {"predictor.line_size", 4}}) {}
    SFPredictor predictor{"predictor"};
};

class SFPredictorHashedTest : public TestModule
{
    void SetUp() override { archx::Instruction::set_decoders({"furiosa"}); }

protected:
    SFPredictorHashedTest()
        : TestModule{{"predictor.hash_address", true},
                     {"predictor.sets", 512},
                     {"predictor.ways", 1},
                     {"predictor.line_size", 4}}
    {
    }
    SFPredictor predictor{"predictor"};
};

class SFPredictorResetOnScrubTest : public TestModule
{
    void SetUp() override { archx::Instruction::set_decoders({"furiosa"}); }

protected:
    SFPredictorResetOnScrubTest()
        : TestModule{{"predictor.reset_on_scrub", true},
                     {"predictor.sets", 512},
                     {"predictor.ways", 1},
                     {"predictor.line_size", 4}}
    {
    }
    SFPredictor predictor{"predictor"};
};

core::Uop create_uop(ProgramCounter pc)
{
    auto archx_instr = std::make_shared<archx::InstructionSimple>(pc, 0b00'111'0'01'01'000000000000'00000'00000);
    auto archx_uops = furiosa::decode(archx_instr->opcode());
    auto archx_uop = std::move(archx_uops[0]);
    auto instr = trans_make<core::Instruction>(std::move(archx_instr), nullptr);
    archx_uop->opcode = archx::Uopcode::LDR_B_32;

    return trans_make<core::Uop>(std::move(archx_uop), std::move(instr));
}

TEST_F(SFPredictorTest, Predict)
{
    auto uop = create_uop(0x10_pc);
    EXPECT_TRUE(predictor.predict(uop));
}

TEST_F(SFPredictorTest, Train)
{
    auto uop = create_uop(0x10_pc);
    EXPECT_TRUE(predictor.predict(uop));

    predictor.train(uop, false);
    predictor.train(uop, false);
    EXPECT_FALSE(predictor.predict(uop));
}

TEST_F(SFPredictorTest, RAWScrub)
{
    auto uop = create_uop(0x10_pc);

    predictor.train(uop, false);
    predictor.train(uop, false);
    EXPECT_FALSE(predictor.predict(uop));

    predictor.raw_scrub(uop);
    EXPECT_TRUE(predictor.predict(uop));
}

TEST_F(SFPredictorTest, MultipleEntries)
{
    auto uop10 = create_uop(0x10_pc);
    auto uop14 = create_uop(0x14_pc);

    predictor.train(uop14, false);
    predictor.train(uop14, false);
    predictor.raw_scrub(uop10);
    EXPECT_TRUE(predictor.predict(uop10));
    EXPECT_FALSE(predictor.predict(uop14));
}

TEST_F(SFPredictorTest, AliasedEntries)
{
    auto uop10 = create_uop(0x10_pc);
    auto uop10010 = create_uop(0x10010_pc);
    predictor.raw_scrub(uop10);
    EXPECT_TRUE(predictor.predict(uop10));
    EXPECT_TRUE(predictor.predict(uop10010));
}

TEST_F(SFPredictorTest, LargePC)
{
    auto uop = create_uop(0xfffffffff10_pc);
    predictor.raw_scrub(uop);
    EXPECT_TRUE(predictor.predict(uop));
}

TEST_F(SFPredictorHashedTest, HashedAddressDontAlias)
{
    auto uop10 = create_uop(0x10_pc);
    auto uop110 = create_uop(0x110_pc);

    predictor.train(uop10, false);
    predictor.train(uop10, false);
    predictor.train(uop110, false);
    predictor.train(uop110, false);
    predictor.raw_scrub(uop10);

    EXPECT_TRUE(predictor.predict(uop10));
    EXPECT_FALSE(predictor.predict(uop110));
}

TEST_F(SFPredictorResetOnScrubTest, Scrub)
{
    auto uop10 = create_uop(0x10_pc);
    auto uop18 = create_uop(0x18_pc);

    predictor.train(uop18, false);
    predictor.train(uop18, false);
    EXPECT_FALSE(predictor.predict(uop18));

    predictor.raw_scrub(uop10);
    EXPECT_TRUE(predictor.predict(uop10));
    EXPECT_TRUE(predictor.predict(uop18));
}

} // namespace ampere::core
