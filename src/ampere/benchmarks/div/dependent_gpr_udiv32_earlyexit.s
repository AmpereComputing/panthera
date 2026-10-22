//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
         .align 8
        .global dependent_gpr_udiv32_earlyexit
dependent_gpr_udiv32_earlyexit:
        str x19, [sp, #-16]!    /* push x19 */
        mov x18, 0x100
        mov x17, #5
        mov x19, #6             /* less than x0 -> early out */
loop:
        udiv w1, w17, w19
        udiv w2, w1, w19
        udiv w3, w2, w19
        udiv w4, w3, w19
        udiv w5, w4, w19
        udiv w6, w5, w19
        udiv w7, w6, w19
        udiv w8, w7, w19
        udiv w9, w8, w19
        udiv w11, w9, w19
        udiv w12, w11, w19
        udiv w13, w12, w19
        udiv w14, w13, w19
        udiv w15, w14, w19
        udiv w16, w15, w19
        udiv w17, w16, w19

        subs x18, x18, 1
        bne loop
        ldr  x19, [sp], #16     /* pop x19 */
        ret


