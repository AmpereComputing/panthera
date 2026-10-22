//
// Copyright (c) 2020-2025 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
.align 8
independent_fdivs:
    mov x18, 0x1000
    fmov d0, #7.0
loop:
    fdiv d1, d0, d0
    fdiv d2, d0, d0
    fdiv d3, d0, d0
    fdiv d4, d0, d0
    fdiv d5, d0, d0
    fdiv d6, d0, d0
    fdiv d7, d0, d0
    fdiv d8, d0, d0
    fdiv d9, d0, d0
    fdiv d11, d0, d0
    fdiv d12, d0, d0
    fdiv d13, d0, d0
    fdiv d14, d0, d0
    fdiv d15, d0, d0
    fdiv d16, d0, d0
    fdiv d17, d0, d0
    
    subs x18, x18, 1
    bne loop
    ret
    
.global independent_fdivs
