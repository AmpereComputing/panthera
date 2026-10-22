//
// Copyright (c) 2019-2025 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
.align 8
dependent_fadds:
    mov x18, 0x10000
    fmov d0, #3.0
loop:
    fadd d0, d0, d0
    fadd d0, d0, d0
    fadd d0, d0, d0
    fadd d0, d0, d0
    fadd d0, d0, d0
    fadd d0, d0, d0
    fadd d0, d0, d0
    fadd d0, d0, d0
    fadd d0, d0, d0
    fadd d0, d0, d0
    fadd d0, d0, d0
    fadd d0, d0, d0
    fadd d0, d0, d0
    fadd d0, d0, d0
    fadd d0, d0, d0
    fadd d0, d0, d0

    subs x18, x18, 1
    bne loop
    ret
    
.global dependent_fadds
