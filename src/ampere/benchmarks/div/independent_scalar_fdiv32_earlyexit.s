//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
         .align 8
zero:
        .float 0.0
        .float 0.0
        .float 0.0
        .float 0.0
nine:
        .float 9.0
        .float 9.0
        .float 9.0
        .float 9.0

        .global independent_scalar_fdiv32_earlyexit
independent_scalar_fdiv32_earlyexit:
        mov x18, 0x100
        ldr  q19, zero
        ldr  q20, nine
loop:
        fdiv s1, s19, s20
        fdiv s2, s19, s20
        fdiv s3, s19, s20
        fdiv s4, s19, s20
        fdiv s5, s19, s20
        fdiv s6, s19, s20
        fdiv s7, s19, s20
        fdiv s8, s19, s20
        fdiv s9, s19, s20
        fdiv s11, s19, s20
        fdiv s12, s19, s20
        fdiv s13, s19, s20
        fdiv s14, s19, s20
        fdiv s15, s19, s20
        fdiv s16, s19, s20
        fdiv s17, s19, s20

        subs x18, x18, 1
        bne loop
        ret


