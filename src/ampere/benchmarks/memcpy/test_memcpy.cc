//
// Copyright (c) 2024-2025 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include <gtest/gtest.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <fmt/core.h>

#include "ampere/benchmarks/micro.h"
#include "ampere/benchmarks/common/constants.h"
#include "ampere/benchmarks/common/memory.h"
#include "gtest/gtest-param-test.h"

namespace ampere::benchmarks {

struct PatternsBenchmark : public BenchmarkWithSetup, public ::testing::WithParamInterface<std::tuple<size_t, bool>>
{
    char *data;
    char *dest;
    size_t size;
    bool dest_populated;
    PatternsBenchmark()
    {
        std::tie(size, dest_populated) = GetParam();
        data = new char[size];
        dest = new char[size];
    }
    ~PatternsBenchmark() override
    {
        delete[] data;
        delete[] dest;
    }
    void SetUp() override
    {
        for (size_t i = 0, j = 0; i < size; ++i, j += i)
            data[i] = static_cast<char>(i);

        thrash_cache(BYTES_16M);

        if (dest_populated)
            populate_physical_pages(dest, size);
    }
    void TearDown() override {}
};

TEST_P(PatternsBenchmark, MemCpy)
{
    run_benchmark([this](size_t iter) {
        for (size_t i = 0; i < iter; ++i)
            std::memcpy(dest, data, size);
    });
}

TEST_P(PatternsBenchmark, MemMove)
{
    run_benchmark([this](size_t iter) {
        for (size_t i = 0; i < iter; ++i)
            std::memmove(dest, data, size);
    });
}

TEST_P(PatternsBenchmark, MemSet)
{
    run_benchmark([this](size_t iter) {
        for (size_t i = 0; i < iter; ++i)
            std::memset(dest, 'c', size);
    });
}

template<typename T>
std::string param_names(const testing::TestParamInfo<typename T::ParamType> &info)
{
    if (std::get<1>(info.param))
        return fmt::format("size{}Xpopulated", std::get<0>(info.param));
    return fmt::format("size{}", std::get<0>(info.param));
}

INSTANTIATE_TEST_SUITE_P(MemCpyBenchmark,
                         PatternsBenchmark,
                         ::testing::Combine(::testing::Values(CACHELINE_SIZE, BYTES_1K, BYTES_4K, BYTES_64K, BYTES_1M),
                                            ::testing::Bool()),
                         param_names<PatternsBenchmark>);

GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(Microbenchmark);

} // namespace ampere::benchmarks
