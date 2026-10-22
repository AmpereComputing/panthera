//
// Copyright (c) 2019-2025 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
.data
.align 8
store_data:
    .space 1216, 0

.align 8
.text
independent_fadds_stores:
    mov x18, 0x10000

    adrp x8, store_data
    add  x8, x8, #:lo12:store_data

    fmov d0, #7.0
loop:
    fadd d1, d0, d0
    fadd d2, d0, d0
    str d10, [x8]

    fadd d3, d0, d0
    fadd d4, d0, d0
    str d10, [x8, 8]

    fadd d5, d0, d0
    fadd d6, d0, d0
    str d11, [x8, 16]

    fadd d7, d0, d0
    fadd d8, d0, d0
    str d12, [x8, 24]

    fadd d9, d0, d0
    fadd d11, d0, d0
    str d13, [x8, 32]

    fadd d12, d0, d0
    fadd d13, d0, d0
    str d14, [x8, 40]

    fadd d14, d0, d0
    fadd d15, d0, d0
    str d15, [x8, 48]

    fadd d16, d0, d0
    fadd d17, d0, d0
    str d16, [x8, 56]

    subs x18, x18, 1
    bne loop
    ret

.global independent_fadds_stores
