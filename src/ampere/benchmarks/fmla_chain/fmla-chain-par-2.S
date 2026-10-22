//
// Copyright (c) 2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

.globl fmla_chain_par_2

.align 6
fmla_chain_par_2:
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

    // 2-way parallelized version of fmla chain. Accumulate
    // the results of multiplying values in v regsiters into
    // two parallel streams in v23 and v24.

    fmla    v23.2d, v6.2d, v6.2d
    fmla    v24.2d, v18.2d, v18.2d
    fmla    v23.2d, v21.2d, v21.2d
    fmla    v24.2d, v22.2d, v22.2d

    sub x0,x0, #1
    cbnz x0, .Lfmla_loop

    // Combine the two streams into one result at the end of
    // the main loop and store the result in v7
    fadd    v23.2d, v23.2d, v24.2d
    fadd    v7.2d, v23.2d, v24.2d
    ret
