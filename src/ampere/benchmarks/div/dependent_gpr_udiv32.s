//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
         .align 8
        .global dependent_gpr_udiv32
dependent_gpr_udiv32:
        str x19, [sp, #-16]!    /* push x19 */
        mov x18, 0x100
        mov x17, #3
        mov x19, #9
loop:
        udiv w1, w19, w17       /* 9/3 = 3 */
        udiv w2, w19, w1
        udiv w3, w19, w2
        udiv w4, w19, w3
        udiv w5, w19, w4
        udiv w6, w19, w5
        udiv w7, w19, w6
        udiv w8, w19, w7
        udiv w9, w19, w8
        udiv w11, w19, w9
        udiv w12, w19, w11
        udiv w13, w19, w12
        udiv w14, w19, w13
        udiv w15, w19, w14
        udiv w16, w19, w15
        udiv w17, w19, w16

        subs x18, x18, 1
        bne loop
        ldr  x19, [sp], #16     /*  pop x19 */
        ret


