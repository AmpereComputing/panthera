//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
         .align 8
data:
        .double 2.2
        .double 2.2

        .global independent_advsimd_fdiv64
independent_advsimd_fdiv64:
        mov x18, 0x100
        ldr  q0, data
loop:
        fdiv v1.2d, v0.2d, v0.2d
        fdiv v2.2d, v0.2d, v0.2d
        fdiv v3.2d, v0.2d, v0.2d
        fdiv v4.2d, v0.2d, v0.2d
        fdiv v5.2d, v0.2d, v0.2d
        fdiv v6.2d, v0.2d, v0.2d
        fdiv v7.2d, v0.2d, v0.2d
        fdiv v8.2d, v0.2d, v0.2d
        fdiv v9.2d, v0.2d, v0.2d
        fdiv v10.2d, v0.2d, v0.2d
        fdiv v11.2d, v0.2d, v0.2d
        fdiv v12.2d, v0.2d, v0.2d
        fdiv v13.2d, v0.2d, v0.2d
        fdiv v14.2d, v0.2d, v0.2d
        fdiv v15.2d, v0.2d, v0.2d
        fdiv v16.2d, v0.2d, v0.2d
        fdiv v17.2d, v0.2d, v0.2d

        subs x18, x18, 1
        bne loop
        ret


