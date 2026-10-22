//
// Copyright (c) 2024-2025 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include "ampere/benchmarks/common/constants.h"

#define CODE_SIZE 1024
#define PREFETCH_ITER (CODE_SIZE * INSTRUCTION_SIZE / CACHELINE_SIZE)

prefetch_code:
    mov x12, x0
loop:
    mov x10, PREFETCH_ITER
    adr x11, nops
prefetch:
    prfm PLIL1KEEP, [x10]
    add x11, x11, CACHELINE_SIZE
    subs x10, x10, 1
    bne prefetch

nops:
.rept CODE_SIZE
    nop
.endr

    subs x12, x12, 1
    bne loop
    ret

.global prefetch_code
