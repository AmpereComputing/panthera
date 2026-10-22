//
// Copyright (c) 2024 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include <gtest/gtest.h>
#include <cstdint>
#include <cstring>
#include <fmt/core.h>
#include <list>

#include "ampere/benchmarks/micro.h"
#include "ampere/benchmarks/common/constants.h"

namespace ampere::benchmarks {

template<typename T>
struct ListBenchmark : public BenchmarkWithSetup, public ::testing::WithParamInterface<std::tuple<size_t, size_t>>
{
    size_t ops;
    size_t size;
    std::list<T> data;
    ListBenchmark() { std::tie(size, ops) = GetParam(); }
    void SetUp() override
    {
        for (size_t i = 0, j = 0; i < size; ++i, j += i)
            data.emplace_back(static_cast<T>(j));
    }
    void TearDown() override {}

    inline void iter_forwards(size_t iter)
    {
        volatile size_t x = 0;
        for (size_t i = 0; i < iter; ++i) {
            size_t y = 0;
            for (auto it = data.begin(); it != data.end(); ++it) {
                auto c = *it;
                for (size_t k = 0; k < ops; ++k)
                    y += c + k;
            }
            x += y;
        }
    }

    inline void iter_backwards(size_t iter)
    {
        volatile size_t x = 0;
        for (size_t i = 0; i < iter; ++i) {
            size_t y = 0;
            for (auto it = data.rbegin(); it != data.rend(); ++it) {
                auto c = *it;
                for (size_t k = 0; k < ops; ++k)
                    y += c + k;
            }
            x += y;
        }
    }
};

using ListofCharsPatternBenchmark = ListBenchmark<char>;
TEST_P(ListofCharsPatternBenchmark, StrideForwards)
{
    run_self_benchmark(*this, &ListofCharsPatternBenchmark::iter_forwards);
}

TEST_P(ListofCharsPatternBenchmark, StrideBackwards)
{
    run_self_benchmark(*this, &ListofCharsPatternBenchmark::iter_backwards);
}

template<typename T>
std::string param_names(const testing::TestParamInfo<typename T::ParamType> &info)
{
    return fmt::format("size{}Xops{}", std::get<0>(info.param), std::get<1>(info.param));
}

INSTANTIATE_TEST_SUITE_P(
    ListofCharsPatternBenchmark,
    ListofCharsPatternBenchmark,
    ::testing::Combine(::testing::Values(2, 4, 8, 16, 32, 64, 128, 256, BYTES_1K, BYTES_4K, BYTES_1M, BYTES_4M),
                       ::testing::Values(1, 4, 8, 16, 32)),
    param_names<ListofCharsPatternBenchmark>);

struct Something
{
    char x = 0;
    char y = 0;
    char z = 0;
    Something() = default;
    Something(size_t i) : y(i) {}
    operator size_t() const { return y; }
};

using ListofStructsPatternBenchmark = ListBenchmark<Something>;
TEST_P(ListofStructsPatternBenchmark, StrideForwards)
{
    run_self_benchmark(*this, &ListofStructsPatternBenchmark::iter_forwards);
}

TEST_P(ListofStructsPatternBenchmark, StrideBackwards)
{
    run_self_benchmark(*this, &ListofStructsPatternBenchmark::iter_backwards);
}

INSTANTIATE_TEST_SUITE_P(
    ListofStructsPatternBenchmark,
    ListofStructsPatternBenchmark,
    ::testing::Combine(::testing::Values(2, 4, 8, 16, 32, 64, 128, 256, BYTES_1K, BYTES_4K, BYTES_1M, BYTES_4M),
                       ::testing::Values(1, 4, 8, 16, 32)),
    param_names<ListofStructsPatternBenchmark>);

GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(Microbenchmark);

} // namespace ampere::benchmarks
