//
// Copyright (c) 2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <random>
#include <algorithm>
#include <iterator>
#include <boost/preprocessor/repeat.hpp>
#include <memory>
#include <unistd.h>

#include "ampere/benchmarks/benchmark.h"
#include "ampere/benchmarks/mem_lat.h"

namespace ampere::benchmarks {

using namespace ::testing;

class RandomStridedVector : public LatMemRd
{
protected:
    RandomStridedVector() : LatMemRd() { random_stride_initialize((char **)start_ptr, size, stride); }

    void perform_iterations(size_t read_iterations) const
    {
        char **p = (char **)start_ptr;
        while (read_iterations-- > 0) {
            for (size_t count = one_circuit_count; count > 0; count--) {
#if 1
#define one_load(z, n, load) load;
                BOOST_PP_REPEAT(100,
                                one_load,
                                asm("ld1 {v0.1D}, [%[ptr]]\nmov %[ptr], v0.D[0]" : [ptr] "+r"(
                                    p) : : "v0")); // first arg is ops_per_circuit, but that doesn't work
                                                   // with the macro
#undef one_load
#else
                // std::cout << "p=" << p << std::endl;
                p = (char **)*p;
#endif
            }
            use_pointer(p);
        }
    }

    void OfflineWarmup() override { perform_iterations(offline_warmup_iterations); }

    void OnlineWarmup() override { perform_iterations(online_warmup_iterations); }

    void Cooldown() override { perform_iterations(cooldown_iterations); }

private:
    static size_t compute_random_offset(size_t offset, size_t stride)
    {
        for (; stride; stride -= sizeof(char *)) {
            size_t flip = (offset >> 31) ^ (offset >> 21) ^ (offset >> 2) ^ (offset);
            offset = (offset >> 1) | (flip << 31);
        }
        return offset;
    }

    static void random_stride_initialize(char **start, size_t size, size_t stride)
    {
        std::mt19937 g;
        std::vector<char **> pointers;

        size_t num_pointers_per_stride = stride / sizeof(char *);
        size_t num_pointers = size / stride;
        char **addr = start;
        for (size_t i = 0; i < num_pointers; i++) {
            pointers.push_back(addr);
            addr += num_pointers_per_stride;
        }

        std::shuffle(pointers.begin(), pointers.end(), g);

        auto it = pointers.begin();
        char **first = *it++;
        char **pointer = first;
        while (it != pointers.end()) {
            *pointer = (char *)*it;
            pointer = *it;
            it++;
        }
        *pointer = (char *)first;
    }
};

TEST_P(RandomStridedVector, Reads)
{
    perform_iterations(iterations);
}

INSTANTIATE_TEST_SUITE_P(LatMemVecRdTests,
                         RandomStridedVector,
                         ::testing::Values(std::make_pair(sizeof(char *), sizeof(char *)), // pure dependent loads
                                           std::make_pair(32 * 1024, 64),                  // L1 latency test
                                           std::make_pair(128 * 1024, 64),                 // L2 latency test
                                           std::make_pair(1024 * 1024, 64),                // L2 latency test
                                           std::make_pair((512 + 1024) * 1024, 64),        // L2 latency test
                                           std::make_pair(2 * 1024 * 1024, 64),            // L2 latency test
                                           std::make_pair((512 + 2 * 1024) * 1024, 64),    // trans-L2 latency test
                                           std::make_pair(3 * 1024 * 1024, 64),            // trans-L2 latency test
                                           std::make_pair((512 + 3 * 1024) * 1024, 64),    // trans-L2 latency test
                                           std::make_pair(4 * 1024 * 1024, 64),            // trans-L2 latency test
                                           std::make_pair((512 + 4 * 1024) * 1024, 64),    // trans-L2 latency test
                                           std::make_pair(5 * 1024 * 1024, 64),            // trans-L2 latency test
                                           std::make_pair((512 + 5 * 1024) * 1024, 64),    // trans-L2 latency test
                                           std::make_pair(6 * 1024 * 1024, 64),            // trans-L2 latency test
                                           std::make_pair((512 + 6 * 1024) * 1024, 64),    // trans-L2 latency test
                                           std::make_pair(7 * 1024 * 1024, 64),            // trans-L2 latency test
                                           std::make_pair((512 + 7 * 1024) * 1024, 64),    // trans-L2 latency test
                                           std::make_pair(8 * 1024 * 1024, 64),            // SLC latency test
                                           std::make_pair(128 * 1024 * 1024, 64))          // DDR latency test
);
} // namespace ampere::benchmarks

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
