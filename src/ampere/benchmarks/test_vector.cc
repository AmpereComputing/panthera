//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/benchmarks/benchmark.h"
#include "ampere/benchmarks/vector_tmpl.h"

#ifdef __aarch64__
#include <arm_neon.h>
#endif

namespace ampere {
namespace benchmarks {

using namespace ::testing;

template<typename T, size_t vector_length>
class VectorTest : public Benchmark
{
public:
    static size_t const operations = 100000 * 1024;
    static size_t const iterations = operations / vector_length;
    T result{0};
    const T x{3};
    Vector<T, vector_length> a{5};
    Vector<T, vector_length> b{7};

protected:
    void TearDown() override
    {
        data["operations"] = std::to_string(operations);
        data["iterations"] = std::to_string(iterations);
        data["vector_length"] = std::to_string(vector_length);
        Benchmark::TearDown();
    }
};

// Types for Vectors of different lengths
//
template<typename T>
using VectorTest1K = VectorTest<T, 1024>;
template<typename T>
using VectorTest10K = VectorTest<T, 10 * 1024>;
template<typename T>
using VectorTest100K = VectorTest<T, 100 * 1024>;

template<typename T>
using VectorTest1M = VectorTest<T, 1000 * 1024>;
template<typename T>
using VectorTest10M = VectorTest<T, 10000 * 1024>;

// List of types which we want to test
//
using VectorTypes = Types<char, int, long long, float, double>;

// These macros will create test suites for the specific vector length
// for each of the types listed in VectorTypes
//
TYPED_TEST_CASE(VectorTest1K, VectorTypes);
TYPED_TEST_CASE(VectorTest10K, VectorTypes);
TYPED_TEST_CASE(VectorTest100K, VectorTypes);
TYPED_TEST_CASE(VectorTest1M, VectorTypes);
TYPED_TEST_CASE(VectorTest10M, VectorTypes);

//
// Tests with vector length of 1024
//
TYPED_TEST(VectorTest1K, TestVectorAdd)
{
    for (size_t i = 0; i < this->iterations; i++)
        vector_add(this->a, this->x);
}

TYPED_TEST(VectorTest1K, TestVectorScale)
{
    for (size_t i = 0; i < this->iterations; i++)
        vector_scale(this->a, this->x);
}

TYPED_TEST(VectorTest1K, TestVectorMAC)
{
    for (size_t i = 0; i < this->iterations; i++)
        vector_mac(this->a, this->b, this->x);
}

TYPED_TEST(VectorTest1K, TestVectorSum)
{
    for (size_t i = 0; i < this->iterations; i++)
        this->result = vector_sum(this->a);
}

template<size_t vector_length>
class FloatTest : public VectorTest<float, vector_length>
{
public:
    static size_t const operations = 100000 * 1024;
    static size_t const iterations = operations / vector_length;
    Vector<float, vector_length> a{5};
    Vector<float, vector_length> b{7};

protected:
    void TearDown() override
    {
        this->data["operations"] = std::to_string(operations);
        this->data["iterations"] = std::to_string(iterations);
        this->data["vector_length"] = std::to_string(vector_length);
        Benchmark::TearDown();
    }
};

#ifdef __aarch64__

using FloatTest1K = FloatTest<1024>;

TEST_F(FloatTest1K, TestFloatAccum)
{
    for (size_t i = 0; i < this->iterations; i++) {
        float32x4_t accum{0};
        for (int j = 0; j < 256; j += 4) {
            float32x4_t value = reinterpret_cast<float32x4_t&>(a.data[j]);
            accum = vaddq_f32(accum, value);
        }
        ASSERT_EQ(vgetq_lane_f32(accum, 0), 320.0);
    }
}

#endif

} // namespace benchmarks
} // namespace ampere
