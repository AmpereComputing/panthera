//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
         .align 8
nine:
        .double 9.0
        .double 9.0
three:
        .double 3.0
        .double 3.0

        .global dependent_advsimd_fdiv64
dependent_advsimd_fdiv64:
        mov x18, 0x100
        ldr  q17, three
        ldr  q19, nine
loop:
        fdiv v1.2d, v19.2d, v17.2d
        fdiv v2.2d, v19.2d, v1.2d
        fdiv v3.2d, v19.2d, v2.2d
        fdiv v4.2d, v19.2d, v3.2d
        fdiv v5.2d, v19.2d, v4.2d
        fdiv v6.2d, v19.2d, v5.2d
        fdiv v7.2d, v19.2d, v6.2d
        fdiv v8.2d, v19.2d, v7.2d
        fdiv v9.2d, v19.2d, v8.2d
        fdiv v10.2d, v19.2d, v9.2d
        fdiv v11.2d, v19.2d, v10.2d
        fdiv v12.2d, v19.2d, v11.2d
        fdiv v13.2d, v19.2d, v12.2d
        fdiv v14.2d, v19.2d, v13.2d
        fdiv v15.2d, v19.2d, v14.2d
        fdiv v16.2d, v19.2d, v15.2d
        fdiv v17.2d, v19.2d, v16.2d

        subs x18, x18, 1
        bne loop
        ret


