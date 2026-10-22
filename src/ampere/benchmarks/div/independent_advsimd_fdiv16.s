//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

        /* compile with -march=armv8-a+fp16 */
        .align 8
data:
        .float 2.2              /* using fp32 data as random bits (resulting reasonable fp16 values) */
        .float 2.2
        .float 2.2
        .float 2.2
        .global independent_advsimd_fdiv16
independent_advsimd_fdiv16:
        mov x18, 0x100
        ldr  q0, data
loop:
        fdiv v1.8h, v0.8h, v0.8h
        fdiv v2.8h, v0.8h, v0.8h
        fdiv v3.8h, v0.8h, v0.8h
        fdiv v4.8h, v0.8h, v0.8h
        fdiv v5.8h, v0.8h, v0.8h
        fdiv v6.8h, v0.8h, v0.8h
        fdiv v7.8h, v0.8h, v0.8h
        fdiv v8.8h, v0.8h, v0.8h
        fdiv v9.8h, v0.8h, v0.8h
        fdiv v10.8h, v0.8h, v0.8h
        fdiv v11.8h, v0.8h, v0.8h
        fdiv v12.8h, v0.8h, v0.8h
        fdiv v13.8h, v0.8h, v0.8h
        fdiv v14.8h, v0.8h, v0.8h
        fdiv v15.8h, v0.8h, v0.8h
        fdiv v16.8h, v0.8h, v0.8h
        fdiv v17.8h, v0.8h, v0.8h

        subs x18, x18, 1
        bne loop
        ret


