//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
.align 8
independent_gpr_udiv64:
        mov x18, 0x100
        mov x0, #7
loop:
        udiv x1, x0, x0
        udiv x2, x0, x0
        udiv x3, x0, x0
        udiv x4, x0, x0
        udiv x5, x0, x0
        udiv x6, x0, x0
        udiv x7, x0, x0
        udiv x8, x0, x0
        udiv x9, x0, x0
        udiv x11, x0, x0
        udiv x12, x0, x0
        udiv x13, x0, x0
        udiv x14, x0, x0
        udiv x15, x0, x0
        udiv x16, x0, x0
        udiv x17, x0, x0

        subs x18, x18, 1
        bne loop
        ret

.global independent_gpr_udiv64
