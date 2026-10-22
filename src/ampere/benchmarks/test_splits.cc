//
// Copyright (c) 2024 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/benchmarks/micro.h"

extern "C" {
int load_split_lat(int iterations);
int load_split_bw(int iterations);
}

namespace ampere::benchmarks {

using namespace ::testing;

INSTANTIATE_TEST_SUITE_P(LoadSplitLat, Microbenchmark, ::testing::Values(std::function<int(int)>(load_split_lat)));

INSTANTIATE_TEST_SUITE_P(LoadSplitBW, Microbenchmark, ::testing::Values(std::function<int(int)>(load_split_bw)));
} // namespace ampere::benchmarks
