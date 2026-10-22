//
// Copyright (c) 2024 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
         .align 8
numer:
        .4byte 0,0,0,0
denom:
        .4byte 3, 3, 3, 3

        .global dependent_sve_udiv32_earlyexit
dependent_sve_udiv32_earlyexit:
        mov x18, 0x100          /* iteration count */
        ldr q10, numer           /* z10 */
        ldr q31, denom          /* z31 */
        ptrue p0.D

        mov z0.d, z10.d
loop:
        /*  dependent sequence of divides */
        udiv z0.s, p0/m, z0.s, z31.s
        udiv z0.s, p0/m, z0.s, z31.s
        udiv z0.s, p0/m, z0.s, z31.s
        udiv z0.s, p0/m, z0.s, z31.s

        udiv z0.s, p0/m, z0.s, z31.s
        udiv z0.s, p0/m, z0.s, z31.s
        udiv z0.s, p0/m, z0.s, z31.s
        udiv z0.s, p0/m, z0.s, z31.s

        udiv z0.s, p0/m, z0.s, z31.s
        udiv z0.s, p0/m, z0.s, z31.s
        udiv z0.s, p0/m, z0.s, z31.s
        udiv z0.s, p0/m, z0.s, z31.s

        udiv z0.s, p0/m, z0.s, z31.s
        udiv z0.s, p0/m, z0.s, z31.s
        udiv z0.s, p0/m, z0.s, z31.s
        udiv z0.s, p0/m, z0.s, z31.s

        udiv z0.s, p0/m, z0.s, z31.s
        udiv z0.s, p0/m, z0.s, z31.s
        udiv z0.s, p0/m, z0.s, z31.s
        udiv z0.s, p0/m, z0.s, z31.s

        udiv z0.s, p0/m, z0.s, z31.s
        udiv z0.s, p0/m, z0.s, z31.s
        udiv z0.s, p0/m, z0.s, z31.s
        udiv z0.s, p0/m, z0.s, z31.s

        udiv z0.s, p0/m, z0.s, z31.s
        udiv z0.s, p0/m, z0.s, z31.s
        udiv z0.s, p0/m, z0.s, z31.s
        udiv z0.s, p0/m, z0.s, z31.s

        udiv z0.s, p0/m, z0.s, z31.s
        udiv z0.s, p0/m, z0.s, z31.s
        udiv z0.s, p0/m, z0.s, z31.s
        udiv z0.s, p0/m, z0.s, z31.s

        udiv z0.s, p0/m, z0.s, z31.s
        udiv z0.s, p0/m, z0.s, z31.s
        udiv z0.s, p0/m, z0.s, z31.s
        udiv z0.s, p0/m, z0.s, z31.s

        subs x18, x18, 1
        bne loop
        ret
