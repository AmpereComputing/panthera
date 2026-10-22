//
// Copyright (c) 2024 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

.globl fpmov_lat_spaced

.align 6
fpmov_lat_spaced:
    mov  x13, x0

    movz x9, #0x00000001
    movz x10, #0x00000002
    movz x11, #0x00000004
    movz x12, #0x00000008

.Lroundtrip_gprs_to_fprs:

    fmov d0, x9

        fadd d0, d0, d0
        fadd d0, d0, d0
        fadd d0, d0, d0
        fadd d0, d0, d0
        fadd d0, d0, d0
        fadd d0, d0, d0
        fadd d0, d0, d0
        fadd d0, d0, d0
        fadd d0, d0, d0
        fadd d0, d0, d0
        fadd d0, d0, d0
        fadd d0, d0, d0
        fadd d0, d0, d0
        fadd d0, d0, d0
        fadd d0, d0, d0
        fadd d0, d0, d0

    fmov x9, D0

        add x9, x9, x9
        add x9, x9, x9
        add x9, x9, x9
        add x9, x9, x9
        add x9, x9, x9
        add x9, x9, x9
        add x9, x9, x9
        add x9, x9, x9
        add x9, x9, x9
        add x9, x9, x9
        add x9, x9, x9
        add x9, x9, x9
        add x9, x9, x9
        add x9, x9, x9
        add x9, x9, x9
        add x9, x9, x9

    fmov d0, x9

        fadd d0, d0, d0
        fadd d0, d0, d0
        fadd d0, d0, d0
        fadd d0, d0, d0
        fadd d0, d0, d0
        fadd d0, d0, d0
        fadd d0, d0, d0
        fadd d0, d0, d0
        fadd d0, d0, d0
        fadd d0, d0, d0
        fadd d0, d0, d0
        fadd d0, d0, d0
        fadd d0, d0, d0
        fadd d0, d0, d0
        fadd d0, d0, d0
        fadd d0, d0, d0

    fmov x9, D0

        add x9, x9, x9
        add x9, x9, x9
        add x9, x9, x9
        add x9, x9, x9
        add x9, x9, x9
        add x9, x9, x9
        add x9, x9, x9
        add x9, x9, x9
        add x9, x9, x9
        add x9, x9, x9
        add x9, x9, x9
        add x9, x9, x9
        add x9, x9, x9
        add x9, x9, x9
        add x9, x9, x9
        add x9, x9, x9

    sub x13,x13, #1
    cbnz x13, .Lroundtrip_gprs_to_fprs    
    ret
