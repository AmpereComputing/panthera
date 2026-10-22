//
// Copyright (c) 2024 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
        .align 8
        .data
data:
        .zero 1024*1024
        .align 8
        .text
        .global emu_1
emu_1:

        movz x1, #:abs_g2:data     // bits 32-47, overflow check
        movk x1, #:abs_g1_nc:data  // bits 16-31, no overflow check
        movk x1, #:abs_g0_nc:data
        mov x3, 32
        prfm pldl1keep, [x1, #16]
        prfm pldl1keep, [x1, x3]
        prfm pldl1keep, emu_1
        prfum pldl1keep, [x1, 64]
        bfxil x4, x1, #0, #0x20  // bfm
        sdiv x4, x1, x3
        udiv x4, x1, x3
        sdiv w4, w1, w3
        udiv w4, w1, w3
        subs  x4, x1, x3
        subs  x4, x3, x1
        subs  w4, w1, w3
        subs  w4, w3, w1
        adds  x4, x1, x3
        adds  x4, x3, x1
        adds  w4, w1, w3
        adds  w4, w3, w1
        adrp  x4, emu_1
        adr   x4, emu_1
        asrv  x4, x1, x3
        asrv  w4, w1, w3
        lslv  x4, x1, x3
        lslv  w4, w1, w3
        clz   x4, x1
        clz   w4, w1
        ret


