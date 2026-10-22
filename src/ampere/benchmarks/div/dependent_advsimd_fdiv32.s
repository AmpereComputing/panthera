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

        .global dependent_advsimd_fdiv32
dependent_advsimd_fdiv32:
        mov x18, 0x100
        ldr  q17, three
        ldr  q19, nine
loop:
        fdiv v1.4s, v19.4s, v17.4s
        fdiv v2.4s, v19.4s, v1.4s
        fdiv v3.4s, v19.4s, v2.4s
        fdiv v4.4s, v19.4s, v3.4s
        fdiv v5.4s, v19.4s, v4.4s
        fdiv v6.4s, v19.4s, v5.4s
        fdiv v7.4s, v19.4s, v6.4s
        fdiv v8.4s, v19.4s, v7.4s
        fdiv v9.4s, v19.4s, v8.4s
        fdiv v10.4s, v19.4s, v9.4s
        fdiv v11.4s, v19.4s, v10.4s
        fdiv v12.4s, v19.4s, v11.4s
        fdiv v13.4s, v19.4s, v12.4s
        fdiv v14.4s, v19.4s, v13.4s
        fdiv v15.4s, v19.4s, v14.4s
        fdiv v16.4s, v19.4s, v15.4s
        fdiv v17.4s, v19.4s, v16.4s

        subs x18, x18, 1
        bne loop
        ret


