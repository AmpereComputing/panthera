//
// Copyright (c) 2024 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/benchmarks/micro.h"

extern "C" {
int fpconvert_lat_2stream(int iterations);
int fpconvert_lat_4stream(int iterations);
int fpconvert_lat_t(int iterations);
int fpconvert_lat_w(int iterations);
int fpconvert_lat(int iterations);
int fpmov_lat_2stream(int iterations);
int fpmov_lat_4stream(int iterations);
int fpmov_lat_t(int iterations);
int fpmov_lat_w(int iterations);
int fpmov_lat(int iterations);
int fpmov_lat_spaced(int iterations);
int fpconvert_lat_spaced(int iterations);
}

namespace ampere::benchmarks {

using namespace ::testing;

INSTANTIATE_TEST_SUITE_P(FMOVTests,
                         Microbenchmark,
                         ::testing::Values(std::function<int(int)>(fpconvert_lat_2stream),
                                           std::function<int(int)>(fpconvert_lat_4stream),
                                           std::function<int(int)>(fpconvert_lat_t),
                                           std::function<int(int)>(fpconvert_lat_w),
                                           std::function<int(int)>(fpconvert_lat),
                                           std::function<int(int)>(fpmov_lat_2stream),
                                           std::function<int(int)>(fpmov_lat_4stream),
                                           std::function<int(int)>(fpmov_lat_t),
                                           std::function<int(int)>(fpmov_lat_w),
                                           std::function<int(int)>(fpmov_lat),
                                           std::function<int(int)>(fpmov_lat_spaced),
                                           std::function<int(int)>(fpconvert_lat_spaced)));

} // namespace ampere::benchmarks
