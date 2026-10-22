//
// Copyright (c) 2020-2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
.data
.align 8

mydata:
    .space 1024, 0

.text
mov_movk_movk_fusion:
        stp x15, x16, [sp, #-16]!
        stp x17, x18, [sp, #-16]!
        mov x1, 0x0
        mov x18, 0x10000
loop:
        movz x0, #:abs_g2:mydata
        movk x0, #:abs_g1_nc:mydata
        movk x0, #:abs_g0_nc:mydata
        add x2, x1, x1
        add x3, x1, x1

        movz x0, #:abs_g2:mydata
        movk x0, #:abs_g1_nc:mydata
        movk x0, #:abs_g0_nc:mydata
        add x4, x1, x1
        add x5, x1, x1

        movz x0, #:abs_g2:mydata
        movk x0, #:abs_g1_nc:mydata
        movk x0, #:abs_g0_nc:mydata
        add x6, x1, x1
        add x7, x1, x1

        movz x0, #:abs_g2:mydata
        movk x0, #:abs_g1_nc:mydata
        movk x0, #:abs_g0_nc:mydata
        add x8, x1, x1
        add x9, x1, x1

        movz x0, #:abs_g2:mydata
        movk x0, #:abs_g1_nc:mydata
        movk x0, #:abs_g0_nc:mydata
        add x11, x1, x1
        add x12, x1, x1

        movz x0, #:abs_g2:mydata
        movk x0, #:abs_g1_nc:mydata
        movk x0, #:abs_g0_nc:mydata
        add x13, x1, x1
        add x14, x1, x1

        movz x0, #:abs_g2:mydata
        movk x0, #:abs_g1_nc:mydata
        movk x0, #:abs_g0_nc:mydata
        add x15, x1, x1
        add x16, x1, x1

        movz x0, #:abs_g2:mydata
        movk x0, #:abs_g1_nc:mydata
        movk x0, #:abs_g0_nc:mydata
        add x2, x1, x1
        add x3, x1, x1

        movz x0, #:abs_g2:mydata
        movk x0, #:abs_g1_nc:mydata
        movk x0, #:abs_g0_nc:mydata
        add x4, x1, x1
        add x5, x1, x1

        movz x0, #:abs_g2:mydata
        movk x0, #:abs_g1_nc:mydata
        movk x0, #:abs_g0_nc:mydata
        add x6, x1, x1
        add x7, x1, x1

        movz x0, #:abs_g2:mydata
        movk x0, #:abs_g1_nc:mydata
        movk x0, #:abs_g0_nc:mydata
        add x8, x1, x1
        add x9, x1, x1

        movz x0, #:abs_g2:mydata
        movk x0, #:abs_g1_nc:mydata
        movk x0, #:abs_g0_nc:mydata
        add x11, x1, x1
        add x12, x1, x1

        movz x0, #:abs_g2:mydata
        movk x0, #:abs_g1_nc:mydata
        movk x0, #:abs_g0_nc:mydata
        add x13, x1, x1
        add x14, x1, x1

        movz x0, #:abs_g2:mydata
        movk x0, #:abs_g1_nc:mydata
        movk x0, #:abs_g0_nc:mydata
        add x15, x1, x1
        add x16, x1, x1

        subs x18, x18, 1
        bne loop
        ldp x17, x18, [sp], #16
        ldp x15, x16, [sp], #16
        ret

.global mov_movk_movk_fusion

