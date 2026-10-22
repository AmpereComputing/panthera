//
// Copyright (c) 2019-2025 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
.align 8
independent_fadds:
    mov x18, 0x10000
    fmov d0, #7.0
loop:
    fadd d1, d0, d0
    fadd d2, d0, d0
    fadd d3, d0, d0
    fadd d4, d0, d0
    fadd d5, d0, d0
    fadd d6, d0, d0
    fadd d7, d0, d0
    fadd d8, d0, d0
    fadd d9, d0, d0
    fadd d11, d0, d0
    fadd d12, d0, d0
    fadd d13, d0, d0
    fadd d14, d0, d0
    fadd d15, d0, d0
    fadd d16, d0, d0
    fadd d17, d0, d0
    
    subs x18, x18, 1
    bne loop
    ret
    
.global independent_fadds
