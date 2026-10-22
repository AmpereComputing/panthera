//
// Copyright (c) 2024 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/benchmarks/micro.h"

extern "C" {
int slow_integer_a(int iterations);
int slow_integer_b(int iterations);
}

namespace ampere::benchmarks {

using namespace ::testing;

INSTANTIATE_TEST_SUITE_P(SlowInteger,
                         Microbenchmark,
                         ::testing::Values(std::function<int(int)>(slow_integer_a),
                                           std::function<int(int)>(slow_integer_b)));
} // namespace ampere::benchmarks
