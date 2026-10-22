//
// Copyright (c) 2024 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/benchmarks/common/constants.h"
#include "ampere/benchmarks/micro.h"

extern "C" {
int prefetch_code(int iterations);
int prefetch_load_data_l1(char *data, int iterations);
int prefetch_load_data_l2(char *data, int iterations);
int prefetch_store_data_l1(char *data, int iterations);
int prefetch_store_data_l2(char *data, int iterations);
int prefetch_streaming_store_data_l1(char *data, int iterations);
int prefetch_streaming_store_data_l2(char *data, int iterations);
}

namespace ampere::benchmarks {

using namespace ::testing;

INSTANTIATE_TEST_SUITE_P(PRFMCode, Microbenchmark, ::testing::Values(micro_function(prefetch_code)));

struct PRFMDataBenchmark : public MicrobenchmarkWithMemory
{
    PRFMDataBenchmark() { setup_memory(this->iterations * CACHELINE_SIZE * 16); }
};

TEST_P(PRFMDataBenchmark, Measure)
{
    if (iterations)
        test_function(test_data_array, iterations);
}

INSTANTIATE_TEST_SUITE_P(PRFMDataBenchmark,
                         PRFMDataBenchmark,
                         ::testing::Values(memory_micro_function(prefetch_load_data_l1),
                                           memory_micro_function(prefetch_load_data_l2),
                                           memory_micro_function(prefetch_store_data_l1),
                                           memory_micro_function(prefetch_store_data_l2),
                                           memory_micro_function(prefetch_streaming_store_data_l1),
                                           memory_micro_function(prefetch_streaming_store_data_l2)));
} // namespace ampere::benchmarks
