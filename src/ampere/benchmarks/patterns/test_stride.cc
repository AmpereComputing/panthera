//
// Copyright (c) 2024 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include <gtest/gtest.h>
#include <cstdint>
#include <cstring>
#include <fmt/core.h>

#include "ampere/benchmarks/micro.h"
#include "ampere/benchmarks/common/constants.h"

namespace ampere::benchmarks {

template<typename T>
struct PatternsBenchmark : public BenchmarkWithSetup,
                           public ::testing::WithParamInterface<std::tuple<size_t, size_t, size_t>>
{
    size_t step;
    size_t steps;
    size_t ops;
    size_t size;
    T *data;
    PatternsBenchmark()
    {
        std::tie(step, steps, ops) = GetParam();
        size = step * steps;
        data = new T[size];
    }
    ~PatternsBenchmark() override { delete[] data; }
    void SetUp() override
    {
        for (size_t i = 0, j = 0; i < size; ++i, j += i)
            data[i] = static_cast<T>(j);
    }
    void TearDown() override {}

    inline void stride_forwards(size_t iter)
    {
        volatile size_t x = 0;
        for (size_t i = 0; i < iter; ++i) {
            size_t y = 0;
            for (size_t j = 0, s = 0; s < steps; j += step, ++s) {
                auto c = data[j];
                for (size_t k = 0; k < ops; ++k)
                    y += c + k;
            }
            x += y;
        }
    }

    inline void stride_backwards(size_t iter)
    {
        volatile size_t x = 0;
        for (size_t i = 0; i < iter; ++i) {
            size_t y = 0;
            for (size_t j = size - 1, s = 0; s < steps; j -= step, ++s) {
                auto c = data[j];
                for (size_t k = 0; k < ops; ++k)
                    y += c + k;
            }
            x += y;
        }
    }
};

using CharArrayPatternBenchmark = PatternsBenchmark<char>;
TEST_P(CharArrayPatternBenchmark, StrideForwards)
{
    run_self_benchmark(*this, &CharArrayPatternBenchmark::stride_forwards);
}

TEST_P(CharArrayPatternBenchmark, StrideBackwards)
{
    run_self_benchmark(*this, &CharArrayPatternBenchmark::stride_backwards);
}

template<typename T>
std::string param_names(const testing::TestParamInfo<typename T::ParamType> &info)
{
    return fmt::format(
        "step{}Xsteps{}Xops{}", std::get<0>(info.param), std::get<1>(info.param), std::get<2>(info.param));
}

INSTANTIATE_TEST_SUITE_P(CharArrayPatternBenchmark,
                         CharArrayPatternBenchmark,
                         ::testing::Combine(::testing::Values(1, 2, 4, 8, 16, 32, 64, 128, 256, BYTES_1K, BYTES_4K),
                                            ::testing::Values(8, 16, 64, 128, 512, BYTES_4K, BYTES_64K),
                                            ::testing::Values(1, 4, 8, 16, 32)),
                         param_names<CharArrayPatternBenchmark>);

struct Something
{
    char x = 0;
    char y = 0;
    char z = 0;
    Something() = default;
    Something(size_t i) : y(i) {}
    operator size_t() const { return y; }
};

using ArrayOfStructsPatternBenchmark = PatternsBenchmark<Something>;
TEST_P(ArrayOfStructsPatternBenchmark, StrideForwards)
{
    run_self_benchmark(*this, &ArrayOfStructsPatternBenchmark::stride_forwards);
}

TEST_P(ArrayOfStructsPatternBenchmark, StrideBackwards)
{
    run_self_benchmark(*this, &ArrayOfStructsPatternBenchmark::stride_backwards);
}

INSTANTIATE_TEST_SUITE_P(ArrayOfStructsPatternBenchmark,
                         ArrayOfStructsPatternBenchmark,
                         ::testing::Combine(::testing::Values(1, 2, 4, 8, 16, 32, 64, 128, 256, BYTES_1K, BYTES_4K),
                                            ::testing::Values(8, 16, 64, 128, 512, BYTES_4K, BYTES_64K),
                                            ::testing::Values(1, 4, 8, 16, 32)),
                         param_names<ArrayOfStructsPatternBenchmark>);

GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(Microbenchmark);

} // namespace ampere::benchmarks
