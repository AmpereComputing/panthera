//
// Copyright (c) 2022-2024 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
         .align 8
numer:
        .8byte 14348907,14348907
denom:
        .8byte 3,3

        .global independent_sve_udiv64
independent_sve_udiv64:
        mov x18, 0x100

        ldr  q0, denom
        ldr  q1, numer
        mov z2.d, z1.d
        mov z3.d, z1.d
        mov z4.d, z1.d
        mov z5.d, z1.d
        mov z6.d, z1.d
        mov z7.d, z1.d
        mov z8.d, z1.d
        mov z9.d, z1.d
        mov z10.d, z1.d
        mov z11.d, z1.d
        mov z12.d, z1.d
        mov z13.d, z1.d
        mov z14.d, z1.d
        mov z15.d, z1.d
        mov z16.d, z1.d
        mov z17.d, z1.d
        mov z18.d, z1.d
        mov z19.d, z1.d
        mov z20.d, z1.d
        mov z21.d, z1.d
        mov z22.d, z1.d
        mov z23.d, z1.d
        mov z24.d, z1.d
        mov z25.d, z1.d
        mov z26.d, z1.d
        mov z27.d, z1.d
        mov z28.d, z1.d
        mov z29.d, z1.d
        mov z30.d, z1.d
        mov z31.d, z1.d

        ptrue p0.d
loop:
        udiv z1.d, p0/m, z1.d, z0.d
        udiv z2.d, p0/m, z2.d, z0.d
        udiv z3.d, p0/m, z3.d, z0.d
        udiv z4.d, p0/m, z4.d, z0.d
        udiv z5.d, p0/m, z5.d, z0.d
        udiv z6.d, p0/m, z6.d, z0.d
        udiv z7.d, p0/m, z7.d, z0.d
        udiv z8.d, p0/m, z8.d, z0.d
        udiv z9.d, p0/m, z9.d, z0.d
        udiv z10.d, p0/m, z10.d, z0.d
        udiv z11.d, p0/m, z11.d, z0.d
        udiv z12.d, p0/m, z12.d, z0.d
        udiv z13.d, p0/m, z13.d, z0.d
        udiv z14.d, p0/m, z14.d, z0.d
        udiv z15.d, p0/m, z15.d, z0.d
        udiv z16.d, p0/m, z16.d, z0.d
        udiv z17.d, p0/m, z17.d, z0.d
        udiv z18.d, p0/m, z18.d, z0.d
        udiv z19.d, p0/m, z19.d, z0.d
        udiv z20.d, p0/m, z20.d, z0.d
        udiv z21.d, p0/m, z21.d, z0.d
        udiv z22.d, p0/m, z22.d, z0.d
        udiv z23.d, p0/m, z23.d, z0.d
        udiv z24.d, p0/m, z24.d, z0.d
        udiv z25.d, p0/m, z25.d, z0.d
        udiv z26.d, p0/m, z26.d, z0.d
        udiv z27.d, p0/m, z27.d, z0.d
        udiv z28.d, p0/m, z28.d, z0.d
        udiv z29.d, p0/m, z29.d, z0.d
        udiv z30.d, p0/m, z30.d, z0.d
        udiv z31.d, p0/m, z31.d, z0.d
        subs x18, x18, 1
        bne loop
        ret


