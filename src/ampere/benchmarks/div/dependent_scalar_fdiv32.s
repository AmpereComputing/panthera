//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
         .align 8
nine:
        .float 9.0
        .float 9.0
        .float 9.0
        .float 9.0
three:
        .float 3.0
        .float 3.0
        .float 3.0
        .float 3.0

        .global dependent_scalar_fdiv32
dependent_scalar_fdiv32:
        mov x18, 0x100
        ldr  q17, three
        ldr  q19, nine
loop:
        fdiv s1, s19, s17       /* 9/3 = 3 */
        fdiv s2, s19, s1
        fdiv s3, s19, s2
        fdiv s4, s19, s3
        fdiv s5, s19, s4
        fdiv s6, s19, s5
        fdiv s7, s19, s6
        fdiv s8, s19, s7
        fdiv s9, s19, s8
        fdiv s11, s19, s9
        fdiv s12, s19, s11
        fdiv s13, s19, s12
        fdiv s14, s19, s13
        fdiv s15, s19, s14
        fdiv s16, s19, s15
        fdiv s17, s19, s16

        subs x18, x18, 1
        bne loop
        ret


