//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include "ampere/benchmarks/benchmark.h"

extern "C" void double_branch_loop();

namespace ampere::benchmarks {

using namespace ::testing;

class Loop : public Benchmark
{};

TEST_F(Loop, DoubleBranchLoop)
{
    double_branch_loop();
}

} // namespace ampere::benchmarks
