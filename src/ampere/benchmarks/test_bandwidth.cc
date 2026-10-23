//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <boost/preprocessor/repeat.hpp>
#include "ampere/benchmarks/benchmark.h"
#include "ampere/benchmarks/micro.h"

extern "C" {
void load_bandwidth(char* data_array_start, size_t data_array_size, unsigned iterations);
}

namespace ampere::benchmarks {

using namespace ::testing;

class Bandwidth : public Benchmark, public ::testing::WithParamInterface<std::pair<size_t, size_t>>, public MemoryMixin
{
protected:
    Bandwidth()
    {
        std::tie(size, iterations) = GetParam();
        setup_memory(size);
    }

    size_t iterations;
};

TEST_P(Bandwidth, Reads)
{
    load_bandwidth(test_data_array, size, iterations);
}

INSTANTIATE_TEST_SUITE_P(BandwidthTests,
                         Bandwidth,
                         ::testing::Values(std::make_pair(32 * 1024, 131072),
                                           std::make_pair(64 * 1024, 65536),
                                           std::make_pair(128 * 1024, 32768),
                                           std::make_pair(256 * 1024, 16384),
                                           std::make_pair(512 * 1024, 8192),
                                           std::make_pair(1024 * 1024, 4096),
                                           std::make_pair(2 * 1024 * 1024, 2048),
                                           std::make_pair(4 * 1024 * 1024, 1024),
                                           std::make_pair(8 * 1024 * 1024, 512),
                                           std::make_pair(16 * 1024 * 1024, 256),
                                           std::make_pair(32 * 1024 * 1024, 128),
                                           std::make_pair(64 * 1024 * 1024, 64),
                                           std::make_pair(128 * 1024 * 1024, 32)));

GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(Microbenchmark);

} // namespace ampere::benchmarks
