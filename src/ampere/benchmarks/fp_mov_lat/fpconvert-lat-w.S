//
// Copyright (c) 2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

.globl fpconvert_lat_w

.align 6
fpconvert_lat_w:
    mov  x13, x0

    movz x9, #0x00000001
    movz x10, #0x00000002
    movz x11, #0x00000004
    movz x12, #0x00000008

.Lroundtrip_gprs_to_fprs:

        // Convert integer value in x9 to fp value in d0
        // and back to x10 and so on through a series of
        // GPR and FP regs to create serialized stream of
        // fp convert instructions. Transit through 4
        // sets of GPR and FP regs per iteration:
        // 
        // x9->D0->x10->D1->x11->D2->x12->D3->x9

    scvtf D0, x9
    fcvtas x10, D0

    scvtf D1, x10
    fcvtas x11, D1

    sub x13,x13, #1

    scvtf D2, x11
    fcvtas x12, D2

    scvtf D3, x12
    fcvtas x9, D3

    cbnz x13, .Lroundtrip_gprs_to_fprs    
    ret
