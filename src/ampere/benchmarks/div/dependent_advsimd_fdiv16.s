//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
         .align 8
nine:
        .short 0x4880
        .short 0x4880
        .short 0x4880
        .short 0x4880
        .short 0x4880
        .short 0x4880
        .short 0x4880
        .short 0x4880

three:
        .short 0x4200
        .short 0x4200
        .short 0x4200
        .short 0x4200
        .short 0x4200
        .short 0x4200
        .short 0x4200
        .short 0x4200

        .global dependent_advsimd_fdiv16
dependent_advsimd_fdiv16:
        mov x18, 0x100
        ldr  q17, three
        ldr  q19, nine
loop:
        fdiv v1.8h, v19.8h, v17.8h
        fdiv v2.8h, v19.8h, v1.8h
        fdiv v3.8h, v19.8h, v2.8h
        fdiv v4.8h, v19.8h, v3.8h
        fdiv v5.8h, v19.8h, v4.8h
        fdiv v6.8h, v19.8h, v5.8h
        fdiv v7.8h, v19.8h, v6.8h
        fdiv v8.8h, v19.8h, v7.8h
        fdiv v9.8h, v19.8h, v8.8h
        fdiv v10.8h, v19.8h, v9.8h
        fdiv v11.8h, v19.8h, v10.8h
        fdiv v12.8h, v19.8h, v11.8h
        fdiv v13.8h, v19.8h, v12.8h
        fdiv v14.8h, v19.8h, v13.8h
        fdiv v15.8h, v19.8h, v14.8h
        fdiv v16.8h, v19.8h, v15.8h
        fdiv v17.8h, v19.8h, v16.8h

        subs x18, x18, 1
        bne loop
        ret


