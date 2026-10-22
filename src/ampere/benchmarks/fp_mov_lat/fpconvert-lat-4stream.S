//
// Copyright (c) 2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

.globl fpconvert_lat_4stream

.align 6
fpconvert_lat_4stream:
    mov  x13, x0

    movz x9, #0x00000001
    movz x10, #0x00000002
    movz x11, #0x00000004
    movz x12, #0x00000008

.Lroundtrip_gprs_to_fprs:

        // Simultaneously convert the integer values in x9-x12
        // to D0-D3 and back to create 4 parallel but internally
        // serialized streams of conversion. Repeat 4x per loop
        // iteration:
        //
        // x9->D0->x9->D0->x9->D0->x9->D0->x9
        // x10->D1->x10->D1->x10->D1->x10->D1->x10
        // x11->D2->x11->D2->x11->D2->x11->D2->x11
        // x12->D3->x12->D3->x12->D3->x12->D3->x12

        // stream 1
    scvtf D0, x9
    fcvtas x9, D0
        
        // stream 2
    scvtf D1, x10
    fcvtas x10, D1

        // stream 3
    scvtf D2, x11
    fcvtas x11, D2

        // stream 4
    scvtf D3, x12
    fcvtas x12, D3

        // -----------------------

        // stream 1
    scvtf D0, x9
    fcvtas x9, D0

        // stream 2
    scvtf D1, x10
    fcvtas x10, D1

        // stream 3
    scvtf D2, x11
    fcvtas x11, D2

        // stream 4
    scvtf D3, x12
    fcvtas x12, D3

    sub x13,x13, #1

        // -----------------------

        // stream 1
    scvtf D0, x9
    fcvtas x9, D0

        // stream 2
    scvtf D1, x10
    fcvtas x10, D1

        // stream 3
    scvtf D2, x11
    fcvtas x11, D2

        // stream 4
    scvtf D3, x12
    fcvtas x12, D3

        // -----------------------

        // stream 1
    scvtf D0, x9
    fcvtas x9, D0

        // stream 2
    scvtf D1, x10
    fcvtas x10, D1

        // stream 3
    scvtf D2, x11
    fcvtas x11, D2

        // stream 4
    scvtf D3, x12
    fcvtas x12, D3

    cbnz x13, .Lroundtrip_gprs_to_fprs    
    ret
