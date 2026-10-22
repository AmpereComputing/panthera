//
// Copyright (c) 2023-2025 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include "ampere/benchmarks/common/ops.h"
#include "ampere/tracing_tools/tracing_hints.h"
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>
#include <sys/mman.h>

int main(int argc, char** argv)
{
    const uint64_t kb = 1024;
    const uint64_t limit = kb * kb * kb;
    const size_t alignment = kb * kb * kb;
    int i = 0, j = 0;
    void* p = 0;
    int err_code = posix_memalign(&p, alignment, limit);
    if (err_code != 0) {
        printf("Failed to allocate memory: %s\n", strerror(err_code));
        exit(EXIT_FAILURE);
    }
    if (madvise(p, limit, MADV_HUGEPAGE) != 0) {
        printf("Madvise() failure: %s\n", strerror(err_code));
        exit(EXIT_FAILURE);
    }
    // added volatile to prevent compiler's optimizer from summarizing the loop effects
    volatile uint8_t* q = 0;
    q = (uint8_t*)p;
    // try to take any page faults before tracing
    bzero(p, limit);

    const uint64_t dl1_capacity = 64 * 1024;
    const uint64_t dl1_line_size = 64;
    const uint64_t dl1_cache_lines = dl1_capacity / dl1_line_size;
    const uint64_t cache_wipes_per_inner_loop = 4;
    uint64_t total = 0;
    hint_start_tracing();
    hint_start_online_warmup();
    for (i = 0; i < dl1_cache_lines * cache_wipes_per_inner_loop; i++) { // load L1 and L2 cache once. Avoid L2 misses
        int idx = i * dl1_line_size;
        total += q[idx];
    }
    hint_start_measuring();
    for (j = 0; j < 100; j++) {
        HINT_ITERATION();
        for (i = 0; i < dl1_cache_lines * cache_wipes_per_inner_loop; i++) {
            int idx = i * dl1_line_size;
            total += q[idx + 8 * 0];
            total += q[idx + 8 * 1];
            total += q[idx + 8 * 2];
            total += q[idx + 8 * 3];
        }
    }
    hint_stop_tracing();
    pad_execution();
    printf("total = %lu\n", total);
}
