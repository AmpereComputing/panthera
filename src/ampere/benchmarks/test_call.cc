//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/benchmarks/benchmark.h"

namespace ampere::benchmarks {

using namespace ::testing;

class Call : public Benchmark
{
protected:
    Call()
    {
        if (getenv("BENCHMARK_ITERATIONS"))
            iterations = std::strtoul(getenv("BENCHMARK_ITERATIONS"), nullptr, 10);
    }

    size_t iterations{1000000};
};

static void __attribute__((noinline)) f()
{
    static volatile int count = 0;
    ++count;
}

TEST_F(Call, RepeatedCalls)
{
    size_t iters = iterations;

    while (iters-- > 0) {
        f();
    }
}

} // namespace ampere::benchmarks
