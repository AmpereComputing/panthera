//
// Copyright (c) 2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/benchmarks/benchmark.h"
#include "ampere/benchmarks/common/constants.h"
#include "ampere/benchmarks/common/memory.h"
#include "ampere/tracing_tools/tracing_hints.h"

namespace ampere::benchmarks {

using namespace ::testing;

class BenchmarkWithSetup : public BenchmarkWithOfflineWarmup
{
    static constexpr size_t default_iterations = 10;

protected:
    BenchmarkWithSetup()
    {
        if (getenv("BENCHMARK_OFFLINE_WARMUP_ITERATIONS")) {
            offline_warmup_iterations =
                std::strtoul(getenv("BENCHMARK_OFFLINE_WARMUP_ITERATIONS"), nullptr, default_iterations);
        }

        if (getenv("BENCHMARK_ITERATIONS")) {
            iterations = std::strtoul(getenv("BENCHMARK_ITERATIONS"), nullptr, default_iterations);
        } else {
            iterations = default_iterations;
        }

        if (getenv("BENCHMARK_COOLDOWN_ITERATIONS")) {
            cooldown_iterations = std::strtoul(getenv("BENCHMARK_COOLDOWN_ITERATIONS"), nullptr, default_iterations);
        }
    }

    size_t offline_warmup_iterations{0};
    size_t iterations{0};
    size_t cooldown_iterations{0};

    template<typename F, typename... Args>
    void run_benchmark(F func, Args&&... args)
    {
        start_timing();
        hint_start_tracing();

        if (offline_warmup_iterations)
            func(std::forward<Args>(args)..., offline_warmup_iterations);

        thrash_cache(BYTES_16M);

        hint_start_online_warmup();
        hint_start_measuring();

        if (iterations)
            func(std::forward<Args>(args)..., iterations);

        hint_stop_tracing();

        if (cooldown_iterations)
            func(std::forward<Args>(args)..., cooldown_iterations);

        collect_timing();
    }

    template<typename F, typename T, typename... Args>
    void run_self_benchmark(T& self, F func, Args&&... args)
    {
        start_timing();
        hint_start_tracing();

        if (offline_warmup_iterations)
            (self.*func)(std::forward<Args>(args)..., offline_warmup_iterations);

        hint_start_online_warmup();
        hint_start_measuring();

        if (iterations)
            (self.*func)(std::forward<Args>(args)..., iterations);

        hint_stop_tracing();

        if (cooldown_iterations)
            (self.*func)(std::forward<Args>(args)..., cooldown_iterations);

        collect_timing();
    }
};

template<typename F>
class BenchmarkWithMicro : public BenchmarkWithSetup, public ::testing::WithParamInterface<F>
{
protected:
    BenchmarkWithMicro() { test_function = this->GetParam(); }

    F test_function;
};

// function(int iterations)
using micro_function = std::function<int(int)>;

struct Microbenchmark : public BenchmarkWithMicro<micro_function>
{
    void OfflineWarmup() override
    {
        if (offline_warmup_iterations)
            test_function(offline_warmup_iterations);
    }
    void Cooldown() override
    {
        if (cooldown_iterations)
            test_function(cooldown_iterations);
    }
};

TEST_P(Microbenchmark, Measure)
{
    if (iterations)
        test_function(iterations);
}

// function(char *data, int iterations)
using memory_micro_function = std::function<int(char*, int)>;

struct MemoryMixin
{
    MemoryMixin() = default;
    explicit MemoryMixin(size_t size) { setup_memory(size); }
    ~MemoryMixin() { delete[] test_data_array; }

    void setup_memory(size_t size)
    {
        this->size = size;
        test_data_array = new char[size];

        populate_physical_pages(test_data_array, size);
    }

    size_t size;
    char* test_data_array = nullptr;
};

struct MicrobenchmarkWithMemory : public BenchmarkWithMicro<memory_micro_function>, public MemoryMixin
{
    MicrobenchmarkWithMemory() = default;
    explicit MicrobenchmarkWithMemory(size_t size) : MemoryMixin(size) {}

    void OfflineWarmup() override
    {
        if (offline_warmup_iterations)
            test_function(test_data_array, offline_warmup_iterations);
    }
    void Cooldown() override
    {
        if (cooldown_iterations)
            test_function(test_data_array, cooldown_iterations);
    }
};

} // namespace ampere::benchmarks

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
