//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
         .align 8
        .global dependent_gpr_udiv64
dependent_gpr_udiv64:
        str x19, [sp, #-16]!    /* push x19 */
        mov x18, 0x100
        mov x17, #3
        mov x19, #9
loop:
        udiv x1, x19, x17       /* 9/3 = 3 */
        udiv x2, x19, x1
        udiv x3, x19, x2
        udiv x4, x19, x3
        udiv x5, x19, x4
        udiv x6, x19, x5
        udiv x7, x19, x6
        udiv x8, x19, x7
        udiv x9, x19, x8
        udiv x11, x19, x9
        udiv x12, x19, x11
        udiv x13, x19, x12
        udiv x14, x19, x13
        udiv x15, x19, x14
        udiv x16, x19, x15
        udiv x17, x19, x16

        subs x18, x18, 1
        bne loop
        ldr  x19, [sp], #16     /* pop x19 */
        ret


