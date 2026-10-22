//
// Copyright (c) 2024 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/benchmarks/micro.h"

extern "C" {
int mispredict_returns(int dummy);
}

namespace ampere::benchmarks {

using namespace ::testing;

INSTANTIATE_TEST_SUITE_P(MispredictLatency,
                         Microbenchmark,
                         ::testing::Values(std::function<int(int)>(mispredict_returns)));
} // namespace ampere::benchmarks
