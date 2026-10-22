//
// Copyright (c) 2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

.globl fmla_chain_par_4

.align 6
fmla_chain_par_4:
    mov  x13, x0

    fmov    v6.2d, #1.0
    fmov    v7.2d, #1.0
    fmov    v18.2d, #1.0
    fmov    v21.2d, #1.0
    fmov    v22.2d, #1.0
    fmov    v23.2d, #1.0
    fmov    v24.2d, #1.0
    fmov    v25.2d, #1.0
    fmov    v26.2d, #1.0

.align 3
.Lfmla_loop:

    // 4-way parallelized version of fmla chain. Accumulate
    // the results of multiplying values in v regsiters into
    // four parallel streams in v23, v24, v25, and v26.

    fmla    v23.2d, v6.2d, v6.2d
    fmla    v24.2d, v18.2d, v18.2d
    fmla    v25.2d, v21.2d, v21.2d
    fmla    v26.2d, v22.2d, v22.2d

    sub x0,x0, #1
    cbnz x0, .Lfmla_loop

    // Combine two of the parallel streams with each other
    // to halve the total number of parallel streams
    fadd    v23.2d, v23.2d, v24.2d
    fadd    v25.2d, v25.2d, v26.2d
    // Combine those two remaining streams into one value
    // in v7
    fadd    v7.2d, v23.2d, v25.2d
    ret
