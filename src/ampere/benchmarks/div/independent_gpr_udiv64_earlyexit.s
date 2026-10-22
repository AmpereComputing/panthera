//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
         .align 8
        .global independent_gpr_udiv64_earlyexit
independent_gpr_udiv64_earlyexit:
        str x19, [sp, #-16]!    /* push x19*/
        mov x18, 0x100
        mov x0, #7
        mov x19, #6             /* less than x0 -> early out*/
loop:
        udiv x1, x19, x0
        udiv x2, x19, x0
        udiv x3, x19, x0
        udiv x4, x19, x0
        udiv x5, x19, x0
        udiv x6, x19, x0
        udiv x7, x19, x0
        udiv x8, x19, x0
        udiv x9, x19, x0
        udiv x11, x19, x0
        udiv x12, x19, x0
        udiv x13, x19, x0
        udiv x14, x19, x0
        udiv x15, x19, x0
        udiv x16, x19, x0
        udiv x17, x19, x0

        subs x18, x18, 1
        bne loop
        ldr  x19, [sp], #16     /* pop x19 */
        ret


