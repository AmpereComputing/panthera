//
// Copyright (c) 2024 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/benchmarks/micro.h"

extern "C" {
int loads_1b(int iterations);
int loads_4b(int iterations);
int loads_8b(int iterations);
int loads_16b(int iterations);
int loads_64b(int iterations);

int stores_1b(int iterations);
int stores_4b(int iterations);
int stores_8b(int iterations);
int stores_16b(int iterations);
int stores_64b(int iterations);
}

namespace ampere::benchmarks {

using namespace ::testing;

INSTANTIATE_TEST_SUITE_P(StreamingMem,
                         Microbenchmark,
                         ::testing::Values(std::function<int(int)>(loads_1b),
                                           std::function<int(int)>(loads_4b),
                                           std::function<int(int)>(loads_8b),
                                           std::function<int(int)>(loads_16b),
                                           std::function<int(int)>(loads_64b),
                                           std::function<int(int)>(stores_1b),
                                           std::function<int(int)>(stores_4b),
                                           std::function<int(int)>(stores_8b),
                                           std::function<int(int)>(stores_16b),
                                           std::function<int(int)>(stores_64b)));

} // namespace ampere::benchmarks
