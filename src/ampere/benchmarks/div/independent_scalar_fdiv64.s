//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
         .align 8
nine:
        .double 9.0
        .double 9.0

        .global independent_scalar_fdiv64
independent_scalar_fdiv64:
        mov x18, 0x100
        ldr  q19, nine
loop:
        fdiv d1, d19, d19
        fdiv d2, d19, d19
        fdiv d3, d19, d19
        fdiv d4, d19, d19
        fdiv d5, d19, d19
        fdiv d6, d19, d19
        fdiv d7, d19, d19
        fdiv d8, d19, d19
        fdiv d9, d19, d19
        fdiv d11, d19, d19
        fdiv d12, d19, d19
        fdiv d13, d19, d19
        fdiv d14, d19, d19
        fdiv d15, d19, d19
        fdiv d16, d19, d19
        fdiv d17, d19, d19

        subs x18, x18, 1
        bne loop
        ret


