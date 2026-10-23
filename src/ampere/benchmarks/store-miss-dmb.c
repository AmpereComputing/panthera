//
// Copyright (c) 2023-2025 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "ampere/benchmarks/common/ops.h"
#include "ampere/tracing_tools/tracing_hints.h"

int main(int argc, char** argv) {
    const uint64_t kb = 1024;
    const uint64_t limit = kb * kb * kb;
    const size_t alignment = kb * kb * kb;
    int i = 0, j = 0;
    void* p = 0;
    volatile char* q = 0;
    int err_code = posix_memalign(&p, alignment, limit);
    if (err_code != 0) {
        printf("Failed to allocate memory: %s\n", strerror(err_code));
        exit(EXIT_FAILURE);
    }
    if (madvise(p, limit, MADV_HUGEPAGE) != 0) {
        printf("Madvise() failure: %s\n", strerror(err_code));
        exit(EXIT_FAILURE);
    }
    q = (char*)p;
    // try to take any page faults before tracing
    bzero(p, limit);

    hint_start_tracing();
    hint_start_online_warmup();
    hint_start_measuring();

    // in a kernel with 64K pages, 2 bits will be translated in our idx
    // computation so 4 sets will be in play if we don't get a transparent huge
    // page allocated.
    for (j = 0; j < 100; j++) {
        for (i = 0; i < 64; i++) {  // 6b    (6+24) = 30 = 1GB
            q[i << 24] = 0;
            asm volatile("dmb sy");
        }
    }
    hint_stop_tracing();
    pad_execution();
}
