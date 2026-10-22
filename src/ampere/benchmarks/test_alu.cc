//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include <boost/preprocessor/repeat.hpp>
#include "ampere/benchmarks/benchmark.h"

extern "C" {
void independent_fadds();
void independent_fmuls();
void dependent_fadds();
void dependent_fmuls();
void isb_test();
void independent_fdivs();
void independent_idivs();
}

namespace ampere::benchmarks {

using namespace ::testing;

class LatALU : public Benchmark
{
protected:
    int initial_value{12};
    double initial_value_double{12.0};
};

TEST_F(LatALU, DependentAdds)
{
    int x = initial_value;

#undef ADD_REG_REG
#ifdef __amd64__
#define ADD_REG_REG __asm__ volatile("add %0,%1" : "=r"(x) : "r"(x))
#elif __aarch64__
#define ADD_REG_REG __asm__ volatile("add %w0,%w1,%w1" : "=r"(x) : "r"(x))
#endif

    for (size_t iterations = 1000000; iterations > 0; --iterations) {
#define one_add(z, n, add) ADD_REG_REG;
        BOOST_PP_REPEAT(100, one_add, nothing)
#undef one_add
    }
}

TEST_F(LatALU, IndependentAdds)
{
    int x = initial_value;
    int y = initial_value;

#undef ADD_REG_REG
#ifdef __amd64__
#define ADD_REG_REG __asm__ volatile("add %0,%1" : "=r"(x) : "r"(y))
#elif __aarch64__
#define ADD_REG_REG __asm__ volatile("add %w0,%w1,%w1" : "=r"(x) : "r"(y))
#endif

    for (size_t iterations = 1000000; iterations > 0; --iterations) {
#define one_add(z, n, add) ADD_REG_REG;
        BOOST_PP_REPEAT(100, one_add, nothing)
#undef one_add
    }
}

TEST_F(LatALU, IndependentFadds)
{
    independent_fadds();
}

TEST_F(LatALU, IndependentFmuls)
{
    independent_fmuls();
}

TEST_F(LatALU, DependentFadds)
{
    dependent_fadds();
}

TEST_F(LatALU, DependentFmuls)
{
    dependent_fmuls();
}

TEST_F(LatALU, IsbTest)
{
    isb_test();
}

TEST_F(LatALU, IndependentFdivs)
{
    independent_fdivs();
}

TEST_F(LatALU, IndependentIdivs)
{
    independent_idivs();
}

} // namespace ampere::benchmarks
