//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
         .align 8
        .global independent_gpr_udiv32_earlyexit
independent_gpr_udiv32_earlyexit:
        str x19, [sp, #-16]!    /* push x19 */
        mov x18, 0x100
        mov x0, #7
        mov x19, #6             /* less than x0 -> early out */
loop:
        udiv w1, w19, w0
        udiv w2, w19, w0
        udiv w3, w19, w0
        udiv w4, w19, w0
        udiv w5, w19, w0
        udiv w6, w19, w0
        udiv w7, w19, w0
        udiv w8, w19, w0
        udiv w9, w19, w0
        udiv w11, w19, w0
        udiv w12, w19, w0
        udiv w13, w19, w0
        udiv w14, w19, w0
        udiv w15, w19, w0
        udiv w16, w19, w0
        udiv w17, w19, w0

        subs x18, x18, 1
        bne loop
        ldr  x19, [sp], #16     /* pop x19 */
        ret


