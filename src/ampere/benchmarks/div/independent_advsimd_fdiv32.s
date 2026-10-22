//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
         .align 8
data:
        .float 2.2
        .float 2.2
        .float 2.2
        .float 2.2
        .global independent_advsimd_fdiv32
independent_advsimd_fdiv32:
        mov x18, 0x100
        ldr  q0, data
loop:
        fdiv v1.4s, v0.4s, v0.4s
        fdiv v2.4s, v0.4s, v0.4s
        fdiv v3.4s, v0.4s, v0.4s
        fdiv v4.4s, v0.4s, v0.4s
        fdiv v5.4s, v0.4s, v0.4s
        fdiv v6.4s, v0.4s, v0.4s
        fdiv v7.4s, v0.4s, v0.4s
        fdiv v8.4s, v0.4s, v0.4s
        fdiv v9.4s, v0.4s, v0.4s
        fdiv v10.4s, v0.4s, v0.4s
        fdiv v11.4s, v0.4s, v0.4s
        fdiv v12.4s, v0.4s, v0.4s
        fdiv v13.4s, v0.4s, v0.4s
        fdiv v14.4s, v0.4s, v0.4s
        fdiv v15.4s, v0.4s, v0.4s
        fdiv v16.4s, v0.4s, v0.4s
        fdiv v17.4s, v0.4s, v0.4s

        subs x18, x18, 1
        bne loop
        ret


