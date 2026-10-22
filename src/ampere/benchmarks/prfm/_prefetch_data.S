//
// Copyright (c) 2024-2025 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include "ampere/benchmarks/common/constants.h"
#include "ampere/benchmarks/common/preprocessor.h"

#define PREFETCH_LOOP_LABEL LABELIZE(prefetch_loop, PREFETCH_TYPE)
#define LOOP_LABEL LABELIZE(loop, PREFETCH_TYPE)

    mov x11, x0
    mov x13, x0
    mov x12, x1
    mov x10, PREFETCH_DISTANCE

PREFETCH_LOOP_LABEL:
    prfm PREFETCH_TYPE, [x11]
    add x11, x11, CACHELINE_SIZE
    subs x10, x10, 1
    bne PREFETCH_LOOP_LABEL

.rept PREFETCH_SPACING
    add x14, x14, 1
.endr

LOOP_LABEL:
    prfm PREFETCH_TYPE, [x11]
    add x11, x11, CACHELINE_SIZE

.rept DEMAND_TO_PREFETCH_RATIO
#ifndef STORES
    ldr x14, [x13]
#else
    str x14, [x13]
#endif
.endr
    add x13, x13, CACHELINE_SIZE

    subs x12, x12, 1
    bne LOOP_LABEL
    ret

#undef PREFETCH_LOOP_LABEL
#undef LOOP_LABEL
