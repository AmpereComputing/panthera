//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
         .align 8
zero:
        .double 0.0
        .double 0.0

inf:
        .quad 0x7FF0000000000000
        .quad 0x7FF0000000000000
        
        .global dependent_scalar_fdiv64_earlyexit
dependent_scalar_fdiv64_earlyexit:
        mov x18, 0x100
        ldr  q17, zero
        ldr  q19, inf
loop:
        fdiv d1, d19, d17
        fdiv d2, d19, d1
        fdiv d3, d19, d2
        fdiv d4, d19, d3
        fdiv d5, d19, d4
        fdiv d6, d19, d5
        fdiv d7, d19, d6
        fdiv d8, d19, d7
        fdiv d9, d19, d8
        fdiv d11, d19, d9
        fdiv d12, d19, d11
        fdiv d13, d19, d12
        fdiv d14, d19, d13
        fdiv d15, d19, d14
        fdiv d16, d19, d15
        fdiv d17, d19, d16

        subs x18, x18, 1
        bne loop
        ret


