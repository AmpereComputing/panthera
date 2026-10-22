//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
         .align 8
zero:
        .short 0x0000
        .short 0x0000
        .short 0x0000
        .short 0x0000
        .short 0x0000
        .short 0x0000
        .short 0x0000
        .short 0x0000
nine:
        .short 0x4880
        .short 0x4880
        .short 0x4880
        .short 0x4880
        .short 0x4880
        .short 0x4880
        .short 0x4880
        .short 0x4880


        .global independent_scalar_fdiv16_earlyexit
independent_scalar_fdiv16_earlyexit:
        mov x18, 0x100
        ldr  q19, zero
        ldr  q20, nine
loop:
        fdiv h1, h19, h20
        fdiv h2, h19, h20
        fdiv h3, h19, h20
        fdiv h4, h19, h20
        fdiv h5, h19, h20
        fdiv h6, h19, h20
        fdiv h7, h19, h20
        fdiv h8, h19, h20
        fdiv h9, h19, h20
        fdiv h11, h19, h20
        fdiv h12, h19, h20
        fdiv h13, h19, h20
        fdiv h14, h19, h20
        fdiv h15, h19, h20
        fdiv h16, h19, h20
        fdiv h17, h19, h20

        subs x18, x18, 1
        bne loop
        ret


