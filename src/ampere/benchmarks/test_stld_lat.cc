//
// Copyright (c) 2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/benchmarks/micro.h"

extern "C" {
int stld_lat(int iterations);
int stld_lat_stpad(int iterations);
int stld_lat_stpad_delay_addr(int iterations);
int together_parallel_load_addr(int iterations);
int data_after_1(int iterations);
int data_after_1_parallel_load_addr(int iterations);
int data_after_2(int iterations);
int data_after_2_parallel_load_addr(int iterations);
int data_after_3(int iterations);
int data_after_3_parallel_load_addr(int iterations);
int data_after_4(int iterations);
int data_after_4_parallel_load_addr(int iterations);
int data_after_5(int iterations);
int data_after_5_parallel_load_addr(int iterations);
int data_after_6(int iterations);
int data_after_6_parallel_load_addr(int iterations);
}

namespace ampere::benchmarks {

using namespace ::testing;

INSTANTIATE_TEST_SUITE_P(StLdFwd,
                         Microbenchmark,
                         ::testing::Values(std::function<int(int)>(stld_lat),
                                           std::function<int(int)>(stld_lat_stpad),
                                           std::function<int(int)>(stld_lat_stpad_delay_addr),
                                           std::function<int(int)>(together_parallel_load_addr),
                                           std::function<int(int)>(data_after_1),
                                           std::function<int(int)>(data_after_1_parallel_load_addr),
                                           std::function<int(int)>(data_after_2),
                                           std::function<int(int)>(data_after_2_parallel_load_addr),
                                           std::function<int(int)>(data_after_3),
                                           std::function<int(int)>(data_after_3_parallel_load_addr),
                                           std::function<int(int)>(data_after_4),
                                           std::function<int(int)>(data_after_4_parallel_load_addr),
                                           std::function<int(int)>(data_after_5),
                                           std::function<int(int)>(data_after_5_parallel_load_addr),
                                           std::function<int(int)>(data_after_6),
                                           std::function<int(int)>(data_after_6_parallel_load_addr)));

} // namespace ampere::benchmarks
