//
// Copyright (c) 2022-2024 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
         .align 8
numer:
        .4byte 0xFFFFFFFE, 0xFFFFFFFE, 0xFFFFFFFE, 0xFFFFFFFE
denom:
        .4byte 3, 3, 3, 3

        .global dependent_sve_udiv32
dependent_sve_udiv32:
        mov x18, 0x100          /* iteration count */
        ldr q10, numer           /* z10 */
        ldr q31, denom          /* z31 */
        ptrue p0.D
loop:
        /*  reload large numerator */
        mov z0.d, z10.d

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

        eor z0.d, z0.d, z0.d  /* zero z0 */
        add z10.d, z10.d, z0.d /* create a dependence for next iteration*/

        subs x18, x18, 1
        bne loop
        ret
