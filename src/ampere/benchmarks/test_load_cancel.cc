//
// Copyright (c) 2024 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/benchmarks/micro.h"

extern "C" {
int load_cancel_ixu(int iterations);
int load_cancel_fsu(int iterations);
}

namespace ampere::benchmarks {

using namespace ::testing;

INSTANTIATE_TEST_SUITE_P(LoadCancel,
                         Microbenchmark,
                         ::testing::Values(std::function<int(int)>(load_cancel_ixu),
                                           std::function<int(int)>(load_cancel_fsu)));
} // namespace ampere::benchmarks
