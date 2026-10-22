//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
.align 8
independent_gpr_udiv32:
        mov x18, 0x100
        mov x0, #7
loop:
        udiv w1, w0, w0
        udiv w2, w0, w0
        udiv w3, w0, w0
        udiv w4, w0, w0
        udiv w5, w0, w0
        udiv w6, w0, w0
        udiv w7, w0, w0
        udiv w8, w0, w0
        udiv w9, w0, w0
        udiv w11, w0, w0
        udiv w12, w0, w0
        udiv w13, w0, w0
        udiv w14, w0, w0
        udiv w15, w0, w0
        udiv w16, w0, w0
        udiv w17, w0, w0

        subs w18, w18, 1
        bne loop
        ret

.global independent_gpr_udiv32
