//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/benchmarks/benchmark.h"
#include <effolkronium/random.hpp>

namespace ampere::benchmarks {

using namespace ::testing;
using Random = effolkronium::random_static;

class Branch : public Benchmark, public WithParamInterface<size_t>
{
protected:
    Branch()
    {
        bits = GetParam();
        assert(bits > 0);

        pattern.resize(bits);

        switch (bits) {
        case 1:
            pattern = {1};
            break;
        case 2:
            pattern = {1, -1};
            break;
        case 3:
            pattern = {1, -1, 1};
            break;
        case 4:
            pattern = {-1, -1, -1, 1};
            break;
        default:
            for (size_t i = 0; i < bits; i++)
                pattern[i] = Random::get<std::int8_t>();
            break;
        }

        if (getenv("BENCHMARK_ITERATIONS"))
            iterations = std::strtoul(getenv("BENCHMARK_ITERATIONS"), nullptr, 10);
    }

    size_t iterations{1000000};
    size_t bits;
    std::vector<std::int8_t> pattern;

    static volatile std::uint64_t use_result_dummy;
};

volatile std::uint64_t Branch::use_result_dummy = 0;

TEST_P(Branch, VariableHistoryConditionalDirect)
{
    size_t index = 0;

    while (iterations-- > 0) {
        if (pattern[index] < 0)
            use_result_dummy++;
        use_result_dummy--;
        index = (index + 1) % bits;
    }
}

INSTANTIATE_TEST_SUITE_P(
    ConditionalDirectBranchTests,
    Branch,
    Values(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 13, 17, 24, 30, 35, 42, 51, 63, 78, 93, 113, 135, 156, 189, 210));

class Indirect : public Benchmark
{
protected:
    Indirect()
    {
        if (getenv("BENCHMARK_ITERATIONS"))
            iterations = std::strtoul(getenv("BENCHMARK_ITERATIONS"), nullptr, 10);

        objs[0] = new A{};
        objs[1] = new B{};
        objs[2] = new C{};
        objs[3] = new D{};
    }

    struct A
    {
        virtual void f() {}
    };

    struct B : public A
    {
        void f() override {}
    };

    struct C : public A
    {
        void f() override {}
    };

    struct D : public A
    {
        void f() override {}
    };

    size_t iterations{1000000};
    A* objs[4];
};

TEST_F(Indirect, IndirectBranchWithFourTargets)
{
    int index = 0;

    while (iterations-- > 0) {
        objs[index]->f();
        index = (index + 1) & 0x3;
    }
}

} // namespace ampere::benchmarks
