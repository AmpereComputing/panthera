//
// Copyright (c) 2019-2025 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
.align 8
dependent_fmuls:
    mov x18, 0x10000
    fmov d0, #2.0
loop:
    fmul d0, d0, d0
    fmul d0, d0, d0
    fmul d0, d0, d0
    fmul d0, d0, d0
    fmul d0, d0, d0
    fmul d0, d0, d0
    fmul d0, d0, d0
    fmul d0, d0, d0
    fmul d0, d0, d0
    fmul d0, d0, d0
    fmul d0, d0, d0
    fmul d0, d0, d0
    fmul d0, d0, d0
    fmul d0, d0, d0
    fmul d0, d0, d0
    fmul d0, d0, d0

    subs x18, x18, 1
    bne loop
    ret
    
.global dependent_fmuls
