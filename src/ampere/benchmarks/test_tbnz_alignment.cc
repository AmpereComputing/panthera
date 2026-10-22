//
// Copyright (c) 2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/benchmarks/micro.h"

extern "C" {
int tbnz_alignment_a00_s016(int iterations);
int tbnz_alignment_a00_s032(int iterations);
int tbnz_alignment_a00_s064(int iterations);
int tbnz_alignment_a00_s128(int iterations);
int tbnz_alignment_a00_s256(int iterations);
int tbnz_alignment_a00_s512(int iterations);
int tbnz_alignment_a04_s016(int iterations);
int tbnz_alignment_a04_s032(int iterations);
int tbnz_alignment_a04_s064(int iterations);
int tbnz_alignment_a04_s128(int iterations);
int tbnz_alignment_a04_s256(int iterations);
int tbnz_alignment_a04_s512(int iterations);
int tbnz_alignment_a08_s016(int iterations);
int tbnz_alignment_a08_s032(int iterations);
int tbnz_alignment_a08_s064(int iterations);
int tbnz_alignment_a08_s128(int iterations);
int tbnz_alignment_a08_s256(int iterations);
int tbnz_alignment_a08_s512(int iterations);
int tbnz_alignment_a12_s016(int iterations);
int tbnz_alignment_a12_s032(int iterations);
int tbnz_alignment_a12_s064(int iterations);
int tbnz_alignment_a12_s128(int iterations);
int tbnz_alignment_a12_s256(int iterations);
int tbnz_alignment_a12_s512(int iterations);
int tbnz_alignment_a16_s016(int iterations);
int tbnz_alignment_a16_s032(int iterations);
int tbnz_alignment_a16_s064(int iterations);
int tbnz_alignment_a16_s128(int iterations);
int tbnz_alignment_a16_s256(int iterations);
int tbnz_alignment_a16_s512(int iterations);
int tbnz_alignment_a20_s016(int iterations);
int tbnz_alignment_a20_s032(int iterations);
int tbnz_alignment_a20_s064(int iterations);
int tbnz_alignment_a20_s128(int iterations);
int tbnz_alignment_a20_s256(int iterations);
int tbnz_alignment_a20_s512(int iterations);
int tbnz_alignment_a24_s016(int iterations);
int tbnz_alignment_a24_s032(int iterations);
int tbnz_alignment_a24_s064(int iterations);
int tbnz_alignment_a24_s128(int iterations);
int tbnz_alignment_a24_s256(int iterations);
int tbnz_alignment_a24_s512(int iterations);
}

namespace ampere::benchmarks {

using namespace ::testing;

INSTANTIATE_TEST_SUITE_P(TBNZAlignment,
                         Microbenchmark,
                         ::testing::Values(std::function<int(int)>(tbnz_alignment_a00_s016),
                                           std::function<int(int)>(tbnz_alignment_a00_s032),
                                           std::function<int(int)>(tbnz_alignment_a00_s064),
                                           std::function<int(int)>(tbnz_alignment_a00_s128),
                                           std::function<int(int)>(tbnz_alignment_a00_s256),
                                           std::function<int(int)>(tbnz_alignment_a00_s512),
                                           std::function<int(int)>(tbnz_alignment_a04_s016),
                                           std::function<int(int)>(tbnz_alignment_a04_s032),
                                           std::function<int(int)>(tbnz_alignment_a04_s064),
                                           std::function<int(int)>(tbnz_alignment_a04_s128),
                                           std::function<int(int)>(tbnz_alignment_a04_s256),
                                           std::function<int(int)>(tbnz_alignment_a04_s512),
                                           std::function<int(int)>(tbnz_alignment_a08_s016),
                                           std::function<int(int)>(tbnz_alignment_a08_s032),
                                           std::function<int(int)>(tbnz_alignment_a08_s064),
                                           std::function<int(int)>(tbnz_alignment_a08_s128),
                                           std::function<int(int)>(tbnz_alignment_a08_s256),
                                           std::function<int(int)>(tbnz_alignment_a08_s512),
                                           std::function<int(int)>(tbnz_alignment_a12_s016),
                                           std::function<int(int)>(tbnz_alignment_a12_s032),
                                           std::function<int(int)>(tbnz_alignment_a12_s064),
                                           std::function<int(int)>(tbnz_alignment_a12_s128),
                                           std::function<int(int)>(tbnz_alignment_a12_s256),
                                           std::function<int(int)>(tbnz_alignment_a12_s512),
                                           std::function<int(int)>(tbnz_alignment_a16_s016),
                                           std::function<int(int)>(tbnz_alignment_a16_s032),
                                           std::function<int(int)>(tbnz_alignment_a16_s064),
                                           std::function<int(int)>(tbnz_alignment_a16_s128),
                                           std::function<int(int)>(tbnz_alignment_a16_s256),
                                           std::function<int(int)>(tbnz_alignment_a16_s512),
                                           std::function<int(int)>(tbnz_alignment_a20_s016),
                                           std::function<int(int)>(tbnz_alignment_a20_s032),
                                           std::function<int(int)>(tbnz_alignment_a20_s064),
                                           std::function<int(int)>(tbnz_alignment_a20_s128),
                                           std::function<int(int)>(tbnz_alignment_a20_s256),
                                           std::function<int(int)>(tbnz_alignment_a20_s512),
                                           std::function<int(int)>(tbnz_alignment_a24_s016),
                                           std::function<int(int)>(tbnz_alignment_a24_s032),
                                           std::function<int(int)>(tbnz_alignment_a24_s064),
                                           std::function<int(int)>(tbnz_alignment_a24_s128),
                                           std::function<int(int)>(tbnz_alignment_a24_s256),
                                           std::function<int(int)>(tbnz_alignment_a24_s512)));

} // namespace ampere::benchmarks
