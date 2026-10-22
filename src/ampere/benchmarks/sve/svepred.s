//
// Copyright (c) 2022-2025 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
        .align 8
        .arch armv8.2-a+fp16+sve
nine:
        .double 9.0
        .double 9.0
three:
        .double 3.0
        .double 3.0

low:
        .short 0x0001
high:
        .short 0x0100

        .global svepred
svepred:
        mov x18, 0x100
        ptrue p0.B
        ptrue p1.H
        ptrue p2.S
        ptrue p3.D
        pfalse p4.B /* no sizes for pfalse */
        adr x0, low
        ldr p5, [x0, #0, MUL VL]
        adr x0, high
        ldr p6, [x0, #0, MUL VL]
        adr x0, nine
loop:
        subs x18, x18, 1
        ld1d  z0.D, p3/z, [x0, #0, MUL VL]  /* predicated on */
        ld1d  z1.D, p4/z, [x0, #0, MUL VL]  /* predicated off */
        ld1d  z2.D, p5/z, [x0, #0, MUL VL]  /* predicated on low */
        ld1d  z3.D, p6/z, [x0, #0, MUL VL]  /* predicated on high  */
        bne loop
        ret
