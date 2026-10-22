//
// Copyright (c) 2024 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/benchmarks/micro.h"

extern "C" {
int cleaving_int_A(int iterations);
int cleaving_int_B(int iterations);
int cleaving_int_C(int iterations);
int cleaving_int_glass_jaw(int iterations);
int cleaving_int_glass_jaw2(int iterations);
}

namespace ampere::benchmarks {

using namespace ::testing;

INSTANTIATE_TEST_SUITE_P(CleavingInteger,
                         Microbenchmark,
                         ::testing::Values(std::function<int(int)>(cleaving_int_A),
                                           std::function<int(int)>(cleaving_int_B),
                                           std::function<int(int)>(cleaving_int_C),
                                           std::function<int(int)>(cleaving_int_glass_jaw),
                                           std::function<int(int)>(cleaving_int_glass_jaw2)));
} // namespace ampere::benchmarks
