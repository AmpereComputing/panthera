//
// Copyright (c) 2022-2024 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
         .align 8
numer:
        .double 14348907.0  /* 3^15 */
        .double 14348907.0  /* 3^15 */
denom:
        .double 3.0
        .double 3.0

        .global dependent_sve_fdiv64
dependent_sve_fdiv64:
        mov x18, 0x100
        ldr  q31, denom
        ldr  q10, numer
        ptrue p0.d
loop:
        mov z0.d, z10.d

        fdiv z0.d, p0/m, z0.d, z31.d
        fdiv z0.d, p0/m, z0.d, z31.d
        fdiv z0.d, p0/m, z0.d, z31.d
        fdiv z0.d, p0/m, z0.d, z31.d

        fdiv z0.d, p0/m, z0.d, z31.d
        fdiv z0.d, p0/m, z0.d, z31.d
        fdiv z0.d, p0/m, z0.d, z31.d
        fdiv z0.d, p0/m, z0.d, z31.d

        fdiv z0.d, p0/m, z0.d, z31.d
        fdiv z0.d, p0/m, z0.d, z31.d
        fdiv z0.d, p0/m, z0.d, z31.d
        fdiv z0.d, p0/m, z0.d, z31.d

        eor z0.d, z0.d, z0.d  /* zero z0 */
        add z10.d, z10.d, z0.d /* create a dependence for next iteration*/

        subs x18, x18, 1
        bne loop
        ret


