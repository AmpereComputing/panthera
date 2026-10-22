//
// Copyright (c) 2024 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/benchmarks/micro.h"

extern "C" {
int l1_thrash_lat(int iterations);
int l1_thrash_bw(int iterations);
}

namespace ampere::benchmarks {

using namespace ::testing;

INSTANTIATE_TEST_SUITE_P(L2TLBTests,
                         Microbenchmark,
                         ::testing::Values(std::function<int(int)>(l1_thrash_lat),
                                           std::function<int(int)>(l1_thrash_bw)));

} // namespace ampere::benchmarks
