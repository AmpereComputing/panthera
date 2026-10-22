//
// Copyright (c) 2019-2025 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
.align 8
independent_fmuls:
    mov x18, 0x40000
    fmov d0, #7.0
loop:
    fmul d1, d0, d0
    fmul d2, d0, d0
    fmul d3, d0, d0
    fmul d4, d0, d0
    fmul d5, d0, d0
    fmul d6, d0, d0
    fmul d7, d0, d0
    fmul d8, d0, d0
    fmul d9, d0, d0
    fmul d11, d0, d0
    fmul d12, d0, d0
    fmul d13, d0, d0
    fmul d14, d0, d0
    fmul d15, d0, d0
    fmul d16, d0, d0
    fmul d17, d0, d0
    
    subs x18, x18, 1
    bne loop
    ret
    
.global independent_fmuls
