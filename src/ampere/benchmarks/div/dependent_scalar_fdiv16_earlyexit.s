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
inf:    
        .short 0x7c00
        .short 0x7c00 
        .short 0x7c00
        .short 0x7c00 
        .short 0x7c00
        .short 0x7c00 
        .short 0x7c00
        .short 0x7c00
        
        .global dependent_scalar_fdiv16_earlyexit
dependent_scalar_fdiv16_earlyexit:
        mov x18, 0x100
        ldr  q17, zero
        ldr  q19, inf
loop:
        fdiv h1, h19, h17
        fdiv h2, h19, h1
        fdiv h3, h19, h2
        fdiv h4, h19, h3
        fdiv h5, h19, h4
        fdiv h6, h19, h5
        fdiv h7, h19, h6
        fdiv h8, h19, h7
        fdiv h9, h19, h8
        fdiv h11, h19, h9
        fdiv h12, h19, h11
        fdiv h13, h19, h12
        fdiv h14, h19, h13
        fdiv h15, h19, h14
        fdiv h16, h19, h15
        fdiv h17, h19, h16

        subs x18, x18, 1
        bne loop
        ret


