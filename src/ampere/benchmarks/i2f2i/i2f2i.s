//
// Copyright (c) 2024 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
        .align 8
        .global i2f2i
i2f2i:
        mov x18, 0x100
loop:
        fmov d0, x9
        fmov x9, d0
        fmov d0, x9
        fmov x9, d0
        fmov d0, x9
        fmov x9, d0
        fmov d0, x9
        fmov x9, d0
        fmov d0, x9
        fmov x9, d0
        fmov d0, x9
        fmov x9, d0
        fmov d0, x9
        fmov x9, d0
        fmov d0, x9
        fmov x9, d0
        fmov d0, x9
        fmov x9, d0
        fmov d0, x9
        fmov x9, d0
        fmov d0, x9
        fmov x9, d0
        fmov d0, x9
        fmov x9, d0
        fmov d0, x9
        fmov x9, d0
        subs x18, x18, 1
        bne loop
        ret


