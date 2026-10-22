//
// Copyright (c) 2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

.globl fpconvert_lat

.align 6
fpconvert_lat:
    mov  x13, x0

    movz x9, #0x00000001
    movz x10, #0x00000002
    movz x11, #0x00000004
    movz x12, #0x00000008

.Lroundtrip_gprs_to_fprs:

        // Convert integer value in x9 to fp value in d0
        // and back to create serialized stream of fp
        // convert instructions. Repeat 4x per loop:
        // 
        // x9->D0->x9->D0->x9->D0->x9->D0->x9

    scvtf D0, x9
    fcvtas x9, D0

        // -----------------------

    scvtf D0, x9
    fcvtas x9, D0

    sub x13,x13, #1

        // -----------------------

    scvtf D0, x9
    fcvtas x9, D0

        // -----------------------

    scvtf D0, x9
    fcvtas x9, D0

    cbnz x13, .Lroundtrip_gprs_to_fprs    
    ret
