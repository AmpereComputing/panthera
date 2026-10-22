//
// Copyright (c) 2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include "ampere/benchmarks/benchmark.h"

extern "C" void rapid_call_ret();

namespace ampere::benchmarks {

using namespace ::testing;

class uRSBTest : public Benchmark
{};

TEST_F(uRSBTest, RapidCallRet)
{
    rapid_call_ret();
}

} // namespace ampere::benchmarks
