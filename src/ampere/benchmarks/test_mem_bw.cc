//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <boost/preprocessor/repeat.hpp>
#include <memory>
#include <unistd.h>

#include "ampere/benchmarks/benchmark.h"

namespace ampere::benchmarks {

using namespace ::testing;

class BwMemRd : public Benchmark, public ::testing::WithParamInterface<std::pair<size_t, size_t>>
{
protected:
    BwMemRd()
    {
        std::tie(size, stride) = GetParam();
        one_circuit_count = size / stride / ops_per_circuit + 1;

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

        data["operations"] = std::to_string(operations);
        data["iterations"] = std::to_string(iterations);

        region.reset(new (std::align_val_t(getpagesize())) char[size]);
        start_ptr = region.get();
    }

    static void use_pointer(void* result) { use_result_dummy += (std::uintptr_t)result; }

    const int ops_per_circuit = 8;

    size_t size{0};
    size_t stride{0};
    size_t iterations{0};
    size_t operations{0};
    size_t one_circuit_count{0};
    char* start_ptr{nullptr};

private:
    std::unique_ptr<char[]> region;
    static volatile std::uintptr_t use_result_dummy;
};

volatile std::uintptr_t BwMemRd::use_result_dummy;

class Strided : public BwMemRd
{
protected:
    Strided() : BwMemRd() { stride_initialize(start_ptr, size, stride); }

private:
    static void stride_initialize(char* addr, size_t size, size_t stride)
    {
        size_t i;

        for (i = 0; i < size; i += stride)
            *(char**)&addr[i] = nullptr;
    }
};

TEST_P(Strided, Reads)
{
    char* p = start_ptr;
    char* v = nullptr;

    while (iterations-- > 0) {
        p = start_ptr;
        for (size_t count = one_circuit_count; count > 0; count--) {
#define one_load(z, n, load) load;
            BOOST_PP_REPEAT(8, one_load, v += *((size_t*)p); p += stride;);
            // first arg is ops_per_circuit, but that doesn't work
            // with the macro
#undef one_load
        }
    }

    p = p + (size_t)v;
    use_pointer(p);
}

INSTANTIATE_TEST_SUITE_P(BwMemRdTests,
                         Strided,
                         ::testing::Values(std::make_pair(32 * 1024, 64),    // L1 bandwidth test
                                           std::make_pair(128 * 1024, 64))); // L2 bandwidth test

class RandomStrided : public BwMemRd
{
public:
    static size_t initial_offset;
    size_t offset_mask;

protected:
    RandomStrided() : BwMemRd()
    {
        // Prepare offset_mask to limit random offsets to within the
        // size of our memory region under test

        // Set only the most significant bit in offset_mask
        offset_mask = -1;
        offset_mask ^= offset_mask >> 1;

        // Search for the most significant set bit in size
        while (offset_mask && !(offset_mask & (size / sizeof(char*)))) {
            offset_mask >>= 1;
        }

        assert(offset_mask);

        // offset_mask now equals 2^{Floor[Log2(size)]}, a power of two
        // of the same order of magnitude as size and something from
        // which we can construct a bit mask:
        offset_mask = offset_mask - 1;

        random_stride_initialize((char**)start_ptr, size, sizeof(char*), offset_mask);
    }

    static inline size_t compute_random_offset(size_t offset)
    {
        size_t flip = (offset >> 31) ^ (offset >> 21) ^ (offset >> 2) ^ (offset);
        return (offset >> 1) | (flip << 31);
    }

private:
    static void random_stride_initialize(char** addr, size_t size, size_t stride, size_t offset_mask)
    {
        size_t i, offset;

        offset = initial_offset;
        for (i = 0; i < size; i += stride) {
            addr[offset & offset_mask] = nullptr;
            offset = compute_random_offset(offset);
        }
    }
};

size_t RandomStrided::initial_offset{0xAFACE0FF};

TEST_P(RandomStrided, Reads)
{
    size_t offset;
    char* p = nullptr;
    char* v = nullptr;

    while (iterations-- > 0) {
        offset = compute_random_offset(initial_offset);
        p = &start_ptr[initial_offset & offset_mask];
        for (size_t count = one_circuit_count; count > 0; count--) {
#define one_load(z, n, load) load;
            BOOST_PP_REPEAT(8, one_load, v += *(size_t*)p; p = &start_ptr[offset & offset_mask];
                            offset = compute_random_offset(offset););
            // first arg is ops_per_circuit, but that doesn't work
            // with the macro
#undef one_load
        }
    }

    p = p + (size_t)v;
    use_pointer(p);
}

INSTANTIATE_TEST_SUITE_P(BwMemRdTests,
                         RandomStrided,
                         ::testing::Values(std::make_pair(32 * 1024, 64),    // L1 latency test
                                           std::make_pair(128 * 1024, 64))); // L2 latency test

} // namespace ampere::benchmarks

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
