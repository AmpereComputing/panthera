//
// Copyright (c) 2019-2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <random>
#include <algorithm>
#include <iterator>
#include <boost/preprocessor/repeat.hpp>
#include <memory>
#include <unistd.h>

#include "ampere/benchmarks/benchmark.h"

namespace ampere::benchmarks {

using namespace ::testing;

class LatMemRd : public BenchmarkWithWarmup, public ::testing::WithParamInterface<std::pair<size_t, size_t>>
{
protected:
    LatMemRd()
    {
        std::tie(size, stride) = GetParam();
        one_circuit_count = size / stride / ops_per_circuit + 1;

        if (getenv("BENCHMARK_OFFLINE_WARMUP_ITERATIONS")) {
            offline_warmup_iterations = std::strtoul(getenv("BENCHMARK_OFFLINE_WARMUP_ITERATIONS"), nullptr, 10);
        }

        if (getenv("BENCHMARK_ONLINE_WARMUP_ITERATIONS")) {
            online_warmup_iterations = std::strtoul(getenv("BENCHMARK_ONLINE_WARMUP_ITERATIONS"), nullptr, 10);
        }

        if (getenv("BENCHMARK_ITERATIONS")) {
            iterations = std::strtoul(getenv("BENCHMARK_ITERATIONS"), nullptr, 10);
            operations = ops_per_circuit * one_circuit_count * iterations;
        } else {
            iterations = 1;

            while (true) {
                operations = ops_per_circuit * one_circuit_count * iterations;
                if (operations >= 10000000)
                    break;
                iterations *= 10;
            }
        }

        if (getenv("BENCHMARK_COOLDOWN_ITERATIONS")) {
            cooldown_iterations = std::strtoul(getenv("BENCHMARK_COOLDOWN_ITERATIONS"), nullptr, 10);
        }

        data["operations"] = std::to_string(operations);
        data["iterations"] = std::to_string(iterations);

        region.reset(new (std::align_val_t(getpagesize())) char[size]);
        start_ptr = region.get();
    }

    static void use_pointer(void* result) { use_result_dummy += (std::uintptr_t)result; }

    void OfflineWarmup() override {}
    void OnlineWarmup() override {}
    void Cooldown() override {}

    const int ops_per_circuit = 100;

    size_t size{0};
    size_t stride{0};
    size_t offline_warmup_iterations{0};
    size_t online_warmup_iterations{0};
    size_t iterations{0};
    size_t cooldown_iterations{0};
    size_t operations{0};
    size_t one_circuit_count{0};
    char* start_ptr{nullptr};

private:
    std::unique_ptr<char[]> region;
    static volatile std::uintptr_t use_result_dummy;
};

volatile std::uintptr_t LatMemRd::use_result_dummy;

} // namespace ampere::benchmarks

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
