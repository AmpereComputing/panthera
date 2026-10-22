//
// Copyright (c) 2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

.globl fmla_chain_par_6

.align 6
fmla_chain_par_6:
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
    fmov    v27.2d, #1.0
    fmov    v28.2d, #1.0

.align 3
.Lfmla_loop:

    // 6-way parallelized version of fmla chain. Accumulate
    // the results of multiplying values in v regsiters into
    // six parallel streams in v23-v28.

    fmla    v23.2d, v6.2d, v6.2d
    fmla    v24.2d, v18.2d, v18.2d
    fmla    v25.2d, v21.2d, v21.2d
    fmla    v26.2d, v22.2d, v22.2d
    fmla    v27.2d, v6.2d, v6.2d
    fmla    v28.2d, v18.2d, v18.2d

    sub x0,x0, #1
    cbnz x0, .Lfmla_loop

    // combine 2 of 6 streams
    fadd    v23.2d, v23.2d, v24.2d
    // combine 2 two more of 6 streams
    fadd    v25.2d, v25.2d, v26.2d
    // combine third pair of 6 streams
    fadd    v27.2d, v27.2d, v28.2d
    // combine 2 of the paired streams
    fadd    v23.2d, v23.2d, v27.2d
    // combine the doubly-paired result just produced
    // with the remainder singly-paired result from
    // above
    fadd    v7.2d, v23.2d, v25.2d
    ret
