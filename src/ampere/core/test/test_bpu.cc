//
// Copyright (c) 2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include "ampere/core/bpu.h"
#include "ampere/sc/testing/test_module.h"

namespace ampere::core {

using namespace ::testing;

class BPUTest : public TestModule
{
protected:
    BPUTest()
        : TestModule{{"bpu.tage.l1_btb.sets", 1024}, {"bpu.tage.l1_btb.ways", 4}, {"bpu.tage.l1_btb.line_size", 64}}
    {
    }

    void SetUp() override
    {
        bpu.prediction.bind(prediction);
        bpu.clear.bind(clear);
        bpu.feq_credit_return.bind(feq_credit_return);
        bpu.bob_alloc.bind(uop_generator);
        bpu.bob_credit_return.bind(bob_credit_releaser);
        bpu.flush.bind(flush);
        bpu.branches_committed.bind(commits);
    }

    BPU bpu{"bpu"};

    NiceMock<MockPutter<Fetch>> prediction;
    NiceMock<MockPutter<Fetch>> clear;
    NiceMock<MockTaker<Credit>> feq_credit_return;

    Producer<Uop> uop_generator;
    CreditReleaser bob_credit_releaser;

    NiceMock<MockReader<Flush>> flush;

    Producer<uint> commits;
};

TEST_F(BPUTest, CanCreateBPU) {}

extern std::pair<int, int> bits_for_rotation_select(size_t num_arrays);
extern size_t prev_pc_to_rotation(std::uint64_t index, size_t num_arrays, int spread);

TEST(BPUComponentTest, TestGlobalRotationSelection)
{
    auto [bits, spread] = bits_for_rotation_select(12);

    EXPECT_EQ(bits, 4);
    EXPECT_EQ(spread, 3);

    for (std::uint64_t idx = 0; idx < 12; idx++)
        EXPECT_EQ(prev_pc_to_rotation(idx, 12, spread), idx);

    EXPECT_EQ(prev_pc_to_rotation(12, 12, spread), 0);
    EXPECT_EQ(prev_pc_to_rotation(13, 12, spread), 3);
    EXPECT_EQ(prev_pc_to_rotation(14, 12, spread), 6);
    EXPECT_EQ(prev_pc_to_rotation(15, 12, spread), 9);
}

} // namespace ampere::core
