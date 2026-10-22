//
// Copyright (c) 2023-2025 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include "ampere/benchmarks/common/ops.h"
#include "ampere/tracing_tools/tracing_hints.h"
#include <stdint.h>
#include <stdlib.h>

int main(int argc, char** argv)
{
    uint64_t x = 0;
    uint64_t zero1 = 0;
    uint64_t zero2 = 0;
    int64_t i = 1024;
    hint_start_tracing();
    hint_start_online_warmup();
    hint_start_measuring();
    for (; i > 0; i--) {
        asm volatile("casl %[compare], %[new], [%[dest]]\n"
                     : /* outputs */[compare] "+r"(zero1)
                     : /* inputs */[new] "r"(zero2), [dest] "p"(&x)
                     : "memory");
    }
    hint_stop_tracing();
    pad_execution();
}
