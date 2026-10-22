//
// Copyright (c) 2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

.globl fmla_chain

.align 6
fmla_chain:
    mov  x13, x0

    fmov    v6.2d, #1.0
    fmov    v7.2d, #1.0
    fmov    v18.2d, #1.0
    fmov    v21.2d, #1.0
    fmov    v22.2d, #1.0
    fmov    v23.2d, #1.0

.Lfmla_loop:

    // Perfectly serialized fmla stream. Each fmla reads v7
    // and adds the current value to the result of the
    // multiplication of two other registers storing the
    // result back into v7

    fmla    v7.2d, v6.2d, v6.2d
    fmla    v7.2d, v18.2d, v18.2d
    fmla    v7.2d, v21.2d, v21.2d
    fmla    v7.2d, v22.2d, v22.2d

    sub x0,x0, #1
    cbnz x0, .Lfmla_loop
    ret
