//
// Copyright (c) 2022-2024 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
         .align 8
numer:
        .4byte 14348907,14348907,14348907,14348907
denom:
        .4byte 3,3,3,3

        .global independent_sve_udiv32
independent_sve_udiv32:
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

        ptrue p0.s
loop:
        udiv z1.s, p0/m, z1.s, z0.s
        udiv z2.s, p0/m, z2.s, z0.s
        udiv z3.s, p0/m, z3.s, z0.s
        udiv z4.s, p0/m, z4.s, z0.s
        udiv z5.s, p0/m, z5.s, z0.s
        udiv z6.s, p0/m, z6.s, z0.s
        udiv z7.s, p0/m, z7.s, z0.s
        udiv z8.s, p0/m, z8.s, z0.s
        udiv z9.s, p0/m, z9.s, z0.s
        udiv z10.s, p0/m, z10.s, z0.s
        udiv z11.s, p0/m, z11.s, z0.s
        udiv z12.s, p0/m, z12.s, z0.s
        udiv z13.s, p0/m, z13.s, z0.s
        udiv z14.s, p0/m, z14.s, z0.s
        udiv z15.s, p0/m, z15.s, z0.s
        udiv z16.s, p0/m, z16.s, z0.s
        udiv z17.s, p0/m, z17.s, z0.s
        udiv z18.s, p0/m, z18.s, z0.s
        udiv z19.s, p0/m, z19.s, z0.s
        udiv z20.s, p0/m, z20.s, z0.s
        udiv z21.s, p0/m, z21.s, z0.s
        udiv z22.s, p0/m, z22.s, z0.s
        udiv z23.s, p0/m, z23.s, z0.s
        udiv z24.s, p0/m, z24.s, z0.s
        udiv z25.s, p0/m, z25.s, z0.s
        udiv z26.s, p0/m, z26.s, z0.s
        udiv z27.s, p0/m, z27.s, z0.s
        udiv z28.s, p0/m, z28.s, z0.s
        udiv z29.s, p0/m, z29.s, z0.s
        udiv z30.s, p0/m, z30.s, z0.s
        udiv z31.s, p0/m, z31.s, z0.s
        subs x18, x18, 1
        bne loop
        ret


