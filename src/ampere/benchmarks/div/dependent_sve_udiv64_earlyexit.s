//
// Copyright (c) 2024 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
         .align 8
numer:
        .8byte 0,0
denom:
        .8byte 3, 3

        .global dependent_sve_udiv64_earlyexit
dependent_sve_udiv64_earlyexit:
        mov x18, 0x100          /* iteration count */
        ldr q10, numer          /* z10 */
        ldr q31, denom          /* z31 */
        ptrue p0.D

        mov z0.d, z10.d
loop:
        /*  dependent sequence of divides */
        udiv z0.d, p0/m, z0.d, z31.d
        udiv z0.d, p0/m, z0.d, z31.d
        udiv z0.d, p0/m, z0.d, z31.d
        udiv z0.d, p0/m, z0.d, z31.d
        udiv z0.d, p0/m, z0.d, z31.d
        udiv z0.d, p0/m, z0.d, z31.d
        udiv z0.d, p0/m, z0.d, z31.d
        udiv z0.d, p0/m, z0.d, z31.d
        udiv z0.d, p0/m, z0.d, z31.d
        udiv z0.d, p0/m, z0.d, z31.d
        udiv z0.d, p0/m, z0.d, z31.d
        udiv z0.d, p0/m, z0.d, z31.d

        udiv z0.d, p0/m, z0.d, z31.d
        udiv z0.d, p0/m, z0.d, z31.d
        udiv z0.d, p0/m, z0.d, z31.d
        udiv z0.d, p0/m, z0.d, z31.d
        udiv z0.d, p0/m, z0.d, z31.d
        udiv z0.d, p0/m, z0.d, z31.d
        udiv z0.d, p0/m, z0.d, z31.d
        udiv z0.d, p0/m, z0.d, z31.d
        udiv z0.d, p0/m, z0.d, z31.d
        udiv z0.d, p0/m, z0.d, z31.d
        udiv z0.d, p0/m, z0.d, z31.d
        udiv z0.d, p0/m, z0.d, z31.d

        udiv z0.d, p0/m, z0.d, z31.d
        udiv z0.d, p0/m, z0.d, z31.d
        udiv z0.d, p0/m, z0.d, z31.d
        udiv z0.d, p0/m, z0.d, z31.d
        udiv z0.d, p0/m, z0.d, z31.d
        udiv z0.d, p0/m, z0.d, z31.d
        udiv z0.d, p0/m, z0.d, z31.d
        udiv z0.d, p0/m, z0.d, z31.d
        udiv z0.d, p0/m, z0.d, z31.d
        udiv z0.d, p0/m, z0.d, z31.d
        udiv z0.d, p0/m, z0.d, z31.d
        udiv z0.d, p0/m, z0.d, z31.d

        subs x18, x18, 1
        bne loop
        ret
