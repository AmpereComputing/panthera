//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
         .align 8
        .global dependent_gpr_udiv64_earlyexit
dependent_gpr_udiv64_earlyexit:
        str x19, [sp, #-16]!    /* push x19 */
        mov x18, 0x100
        mov x17, #5
        mov x19, #6             /* less than x0 -> early out */
loop:
        udiv x1, x17, x19
        udiv x2, x1, x19
        udiv x3, x2, x19
        udiv x4, x3, x19
        udiv x5, x4, x19
        udiv x6, x5, x19
        udiv x7, x6, x19
        udiv x8, x7, x19
        udiv x9, x8, x19
        udiv x11, x9, x19
        udiv x12, x11, x19
        udiv x13, x12, x19
        udiv x14, x13, x19
        udiv x15, x14, x19
        udiv x16, x15, x19
        udiv x17, x16, x19

        subs x18, x18, 1
        bne loop
        ldr  x19, [sp], #16     /* pop x19 */
        ret


