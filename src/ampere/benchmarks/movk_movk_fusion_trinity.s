//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
.data
.align 8

mydata:
    .space 1024, 0

.text
movk_movk_fusion:
        stp x15, x16, [sp, #-16]!
        stp x17, x18, [sp, #-16]!
        stp x19, x20, [sp, #-16]!
        stp x21, x22, [sp, #-16]!
        stp x23, x24, [sp, #-16]!
        stp x25, x26, [sp, #-16]!
        mov x1, 0x0
        mov x23, 0x1000
loop:
        movk x0, #:abs_g1_nc:mydata
        movk x0, #:abs_g2_nc:mydata
        add x2, x1, x1
        add x3, x1, x1
        add x4, x1, x1

        movk x0, #:abs_g1_nc:mydata
        movk x0, #:abs_g2_nc:mydata
        add x5, x1, x1
        add x6, x1, x1
        add x7, x1, x1

        movk x0, #:abs_g1_nc:mydata
        movk x0, #:abs_g2_nc:mydata
        add x8, x1, x1
        add x9, x1, x1
        add x10, x1, x1

        movk x0, #:abs_g1_nc:mydata
        movk x0, #:abs_g2_nc:mydata
        add x11, x1, x1
        add x12, x1, x1
        add x13, x1, x1

        movk x0, #:abs_g1_nc:mydata
        movk x0, #:abs_g2_nc:mydata
        add x14, x1, x1
        add x15, x1, x1
        add x16, x1, x1

        movk x0, #:abs_g1_nc:mydata
        movk x0, #:abs_g2_nc:mydata
        add x17, x1, x1
        add x18, x1, x1
        add x19, x1, x1

        movk x0, #:abs_g1_nc:mydata
        movk x0, #:abs_g2_nc:mydata
        add x20, x1, x1
        add x21, x1, x1
        add x22, x1, x1

        movk x0, #:abs_g1_nc:mydata
        movk x0, #:abs_g2_nc:mydata
        add x2, x1, x1
        add x3, x1, x1
        add x4, x1, x1

        movk x0, #:abs_g1_nc:mydata
        movk x0, #:abs_g2_nc:mydata
        add x5, x1, x1
        add x6, x1, x1
        add x7, x1, x1

        movk x0, #:abs_g1_nc:mydata
        movk x0, #:abs_g2_nc:mydata
        add x8, x1, x1
        add x9, x1, x1
        add x10, x1, x1

        movk x0, #:abs_g1_nc:mydata
        movk x0, #:abs_g2_nc:mydata
        add x11, x1, x1
        add x12, x1, x1
        add x13, x1, x1

        movk x0, #:abs_g1_nc:mydata
        movk x0, #:abs_g2_nc:mydata
        add x14, x1, x1
        add x15, x1, x1
        add x16, x1, x1

        movk x0, #:abs_g1_nc:mydata
        movk x0, #:abs_g2_nc:mydata
        add x17, x1, x1
        add x18, x1, x1
        add x19, x1, x1

        movk x0, #:abs_g1_nc:mydata
        movk x0, #:abs_g2_nc:mydata
        add x20, x1, x1
        add x21, x1, x1
        add x22, x1, x1

        subs x23, x23, 1
        bne loop
        ldp x25, x26, [sp], #16
        ldp x23, x24, [sp], #16
        ldp x21, x22, [sp], #16
        ldp x19, x20, [sp], #16
        ldp x17, x18, [sp], #16
        ldp x15, x16, [sp], #16
        ret

.global movk_movk_fusion
