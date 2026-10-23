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

template<typename T, bool ORDERED>
struct PointerBenchmark : public BenchmarkWithSetup,
                          public ::testing::WithParamInterface<std::tuple<size_t, size_t, size_t>>
{
    size_t step;
    size_t steps;
    size_t ops;
    size_t size;
    T* data;
    T** pointers;
    PointerBenchmark()
    {
        std::tie(step, steps, ops) = GetParam();
        size = step * steps;
        data = new T[size];
        pointers = new T*[size];
    }
    ~PointerBenchmark() override
    {
        delete[] data;
        delete[] pointers;
    }
    void SetUp() override
    {
        for (size_t i = 0, j = 0; i < size; ++i, j += i)
            data[i] = static_cast<T>(j);

        if constexpr (ORDERED) {
            for (size_t i = 0; i < size; ++i)
                pointers[i] = &data[i];
        } else {
            for (size_t i = 0, j = 0; i < size; ++i, j += i)
                pointers[i] = &data[i % size];
        }
    }
    void TearDown() override {}

    inline void stride_forwards(size_t iter)
    {
        volatile size_t x = 0;
        for (size_t i = 0; i < iter; ++i) {
            size_t y = 0;
            for (size_t j = 0, s = 0; s < steps; j += step, ++s) {
                auto c = *pointers[j];
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
                auto c = *pointers[j];
                for (size_t k = 0; k < ops; ++k)
                    y += c + k;
            }
            x += y;
        }
    }
};

using ArrayOfPointersPatternBenchmark = PointerBenchmark<char, false>;
TEST_P(ArrayOfPointersPatternBenchmark, StrideForwards)
{
    run_self_benchmark(*this, &ArrayOfPointersPatternBenchmark::stride_forwards);
}

TEST_P(ArrayOfPointersPatternBenchmark, StrideBackwards)
{
    run_self_benchmark(*this, &ArrayOfPointersPatternBenchmark::stride_backwards);
}

template<typename T>
std::string param_names(const testing::TestParamInfo<typename T::ParamType>& info)
{
    return fmt::format(
        "step{}Xsteps{}Xops{}", std::get<0>(info.param), std::get<1>(info.param), std::get<2>(info.param));
}

INSTANTIATE_TEST_SUITE_P(ArrayOfPointersPatternBenchmark,
                         ArrayOfPointersPatternBenchmark,
                         ::testing::Combine(::testing::Values(1, 2, 4, 8, 16, 32, 64, 128, 256, BYTES_1K, BYTES_4K),
                                            ::testing::Values(8, 16, 64, 128, 512, BYTES_4K, BYTES_64K),
                                            ::testing::Values(1, 4, 8, 16, 32)),
                         param_names<ArrayOfPointersPatternBenchmark>);

using ArrayOfOrderedPointersPatternBenchmark = PointerBenchmark<char, true>;
TEST_P(ArrayOfOrderedPointersPatternBenchmark, StrideForwards)
{
    run_self_benchmark(*this, &ArrayOfOrderedPointersPatternBenchmark::stride_forwards);
}

TEST_P(ArrayOfOrderedPointersPatternBenchmark, StrideBackwards)
{
    run_self_benchmark(*this, &ArrayOfOrderedPointersPatternBenchmark::stride_backwards);
}

INSTANTIATE_TEST_SUITE_P(ArrayOfOrderedPointersPatternBenchmark,
                         ArrayOfOrderedPointersPatternBenchmark,
                         ::testing::Combine(::testing::Values(1, 2, 4, 8, 16, 32, 64, 128, 256, BYTES_1K, BYTES_4K),
                                            ::testing::Values(8, 16, 64, 128, 512, BYTES_4K, BYTES_64K),
                                            ::testing::Values(1, 4, 8, 16, 32)),
                         param_names<ArrayOfPointersPatternBenchmark>);

GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(Microbenchmark);

} // namespace ampere::benchmarks
