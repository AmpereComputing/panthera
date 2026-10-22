//
// Copyright (c) 2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include "ampere/benchmarks/benchmark.h"
#include "ampere/tracing_tools/tracing_hints.h"

extern "C" {
void test_br_immed_mis_pred_retired();
void test_unconditional_branch_miss_btb();
void test_return_mispredict();
void test_indirect_branch_hit_btb_and_mispredict();
}

namespace ampere::benchmarks {

using namespace ::testing;

class BranchPMUs : public ::testing::Test
{};

TEST_F(BranchPMUs, BrImmedMisPredRetired)
{
    hint_start_tracing();
    test_br_immed_mis_pred_retired();
    hint_stop_tracing();
}

TEST_F(BranchPMUs, UnconditionalBranchMissBTB)
{
    test_unconditional_branch_miss_btb();
}

TEST_F(BranchPMUs, ReturnMispredict)
{
    hint_start_tracing();
    test_return_mispredict();
    hint_stop_tracing();
}

TEST_F(BranchPMUs, IndirectBranchHitBTBAndMispredict)
{
    hint_start_tracing();
    test_indirect_branch_hit_btb_and_mispredict();
    hint_stop_tracing();
}

} // namespace ampere::benchmarks
