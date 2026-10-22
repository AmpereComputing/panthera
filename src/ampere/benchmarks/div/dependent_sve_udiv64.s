//
// Copyright (c) 2022-2024 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
         .align 8
numer:
        .8byte 0xFFFFFFFFFFFFFFFE, 0xFFFFFFFFFFFFFFFE
denom:
        .8byte 3, 3

        .global dependent_sve_udiv64
dependent_sve_udiv64:
        mov x18, 0x100          /* iteration count */
        ldr q10, numer          /* z10 */
        ldr q31, denom          /* z31 */
        ptrue p0.D
loop:
        /*  reload numerator */
        mov z0.d, z10.d

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

        eor z0.d, z0.d, z0.d  /* zero z0 */
        add z10.d, z10.d, z0.d /* create a dependence for next iteration*/

        subs x18, x18, 1
        bne loop
        ret
