//
// Copyright (c) 2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

.globl fpmov_lat

.align 6
fpmov_lat:
    mov  x13, x0

    movz x9, #0x00000001
    movz x10, #0x00000002
    movz x11, #0x00000004
    movz x12, #0x00000008

.Lroundtrip_gprs_to_fprs:

    fmov D0, x9
    fmov x9, D0

    fmov D0, x9
    fmov x9, D0

    sub x13,x13, #1

    fmov D0, x9
    fmov x9, D0

    fmov D0, x9
    fmov x9, D0

    cbnz x13, .Lroundtrip_gprs_to_fprs    
    ret
