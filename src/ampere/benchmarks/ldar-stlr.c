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
    uint64_t x = 1;
    uint64_t y = 1;
    int64_t i = 1024;
    hint_start_tracing();
    hint_start_online_warmup();
    hint_start_measuring();
    for (; i > 0; i--) {
        asm volatile(
            "ldar %[lddst], [%[memloc]]\n"
            "stlr %[stsrc], [%[memloc]]\n"
            : /* outputs */[lddst] "=r"(x)
            : /* inputs */[stsrc] "r"(y), [memloc] "p"(&x)
            : "memory");
    }
    hint_stop_tracing();
    pad_execution();
}
