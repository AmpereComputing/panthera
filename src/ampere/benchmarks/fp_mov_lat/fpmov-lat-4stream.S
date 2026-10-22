//
// Copyright (c) 2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

.globl fpmov_lat_4stream

.align 6
fpmov_lat_4stream:
    mov  x13, x0

    movz x9, #0x00000001
    movz x10, #0x00000002
    movz x11, #0x00000004
    movz x12, #0x00000008

.Lroundtrip_gprs_to_fprs:

        // Simultaneously move the bytes in x9-x12 without
        // conversion to D0-D3 and back to create 4 parallel
        // but internally serialized streams of moves between the
        // GPR and FP register files. Repeat 4x per loop
        // iteration:
        //
        // x9->D0->x9->D0->x9->D0->x9->D0->x9
        // x10->D1->x10->D1->x10->D1->x10->D1->x10

        // stream 1
    fmov D0, x9
    fmov x9, D0

        // stream 2
    fmov D1, x10
    fmov x10, D1

        // stream 3
    fmov D2, x11
    fmov x11, D2

        // stream 4
    fmov D3, x12
    fmov x12, D3

        // -----------------------

        // stream 1
    fmov D0, x9
    fmov x9, D0

        // stream 2
    fmov D1, x10
    fmov x10, D1

        // stream 3
    fmov D2, x11
    fmov x11, D2

        // stream 4
    fmov D3, x12
    fmov x12, D3

    sub x13,x13, #1

        // -----------------------

        // stream 1
    fmov D0, x9
    fmov x9, D0

        // stream 2
    fmov D1, x10
    fmov x10, D1

        // stream 3
    fmov D2, x11
    fmov x11, D2

        // stream 4
    fmov D3, x12
    fmov x12, D3

        // -----------------------

        // stream 1
    fmov D0, x9
    fmov x9, D0

        // stream 2
    fmov D1, x10
    fmov x10, D1

        // stream 3
    fmov D2, x11
    fmov x11, D2

        // stream 4
    fmov D3, x12
    fmov x12, D3

    cbnz x13, .Lroundtrip_gprs_to_fprs    
    ret
