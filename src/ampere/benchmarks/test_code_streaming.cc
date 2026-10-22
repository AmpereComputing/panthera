//
// Copyright (c) 2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

// Test the ability to stream large code footprints

#include "ampere/benchmarks/micro.h"

extern "C" {
int code_streaming_8k(int iterations);
int code_streaming_16k(int iterations);
int code_streaming_32k(int iterations);
int code_streaming_64k(int iterations);
int code_streaming_128k(int iterations);
}

namespace ampere::benchmarks {

using namespace ::testing;

INSTANTIATE_TEST_SUITE_P(CodeStreaming,
                         Microbenchmark,
                         ::testing::Values(std::function<int(int)>(code_streaming_8k),
                                           std::function<int(int)>(code_streaming_16k),
                                           std::function<int(int)>(code_streaming_32k),
                                           std::function<int(int)>(code_streaming_64k),
                                           std::function<int(int)>(code_streaming_128k)));
} // namespace ampere::benchmarks
