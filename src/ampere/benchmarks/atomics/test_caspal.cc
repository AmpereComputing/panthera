//
// Copyright (c) 2024 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <gtest/gtest.h>
#include <array>

#include "ampere/benchmarks/micro.h"

namespace ampere::benchmarks {

using namespace ::testing;

template<typename T>
struct CASPALBenchmark : public BenchmarkWithSetup
{
    std::array<T, 2> x;
    std::array<T, 2> y;
    std::array<T, 2> values;
};

using casp_types = ::testing::Types<uint32_t, uint64_t>;
TYPED_TEST_CASE(CASPALBenchmark, casp_types);

#define CASPAL(size)                                                                                \
    asm volatile("caspal %" size "[compare_lo], %" size "[compare_hi], %" size "[store_lo], %" size \
                 "[store_hi], [%[dest]]\n"                                                          \
                 : [compare_lo] "+r"(x0), [compare_hi] "+r"(x1)                                     \
                 : [store_lo] "r"(y0), [store_hi] "r"(y1), [dest] "p"(&this->values)                \
                 : "memory")

TYPED_TEST(CASPALBenchmark, Basic)
{
    register TypeParam x0 __asm("r2") = this->x[0];
    register TypeParam x1 __asm("r3") = this->x[1];
    register TypeParam y0 __asm("r4") = this->y[0];
    register TypeParam y1 __asm("r5") = this->y[1];
    for (size_t i = 0; i < this->iterations; ++i) {
        if constexpr (sizeof(TypeParam) > 4) {
            CASPAL("x");
        } else {
            CASPAL("w");
        }
    }
}

GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(Microbenchmark);

} // namespace ampere::benchmarks
