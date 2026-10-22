//
// Copyright (c) 2024 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
         .align 8
numer:
        .2byte 0,0,0,0, 0,0,0,0
denom:
        .short 0x4200
        .short 0x4200
        .short 0x4200
        .short 0x4200
        .short 0x4200
        .short 0x4200
        .short 0x4200
        .short 0x4200

        .global independent_sve_fdiv16_earlyexit
independent_sve_fdiv16_earlyexit:
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

        ptrue p0.h
loop:
        fdiv z1.h, p0/m, z1.h, z0.h
        fdiv z2.h, p0/m, z2.h, z0.h
        fdiv z3.h, p0/m, z3.h, z0.h
        fdiv z4.h, p0/m, z4.h, z0.h
        fdiv z5.h, p0/m, z5.h, z0.h
        fdiv z6.h, p0/m, z6.h, z0.h
        fdiv z7.h, p0/m, z7.h, z0.h
        fdiv z8.h, p0/m, z8.h, z0.h
        fdiv z9.h, p0/m, z9.h, z0.h
        fdiv z10.h, p0/m, z10.h, z0.h
        fdiv z11.h, p0/m, z11.h, z0.h
        fdiv z12.h, p0/m, z12.h, z0.h
        fdiv z13.h, p0/m, z13.h, z0.h
        fdiv z14.h, p0/m, z14.h, z0.h
        fdiv z15.h, p0/m, z15.h, z0.h
        fdiv z16.h, p0/m, z16.h, z0.h
        fdiv z17.h, p0/m, z17.h, z0.h
        fdiv z18.h, p0/m, z18.h, z0.h
        fdiv z19.h, p0/m, z19.h, z0.h
        fdiv z20.h, p0/m, z20.h, z0.h
        fdiv z21.h, p0/m, z21.h, z0.h
        fdiv z22.h, p0/m, z22.h, z0.h
        fdiv z23.h, p0/m, z23.h, z0.h
        fdiv z24.h, p0/m, z24.h, z0.h
        fdiv z25.h, p0/m, z25.h, z0.h
        fdiv z26.h, p0/m, z26.h, z0.h
        fdiv z27.h, p0/m, z27.h, z0.h
        fdiv z28.h, p0/m, z28.h, z0.h
        fdiv z29.h, p0/m, z29.h, z0.h
        fdiv z30.h, p0/m, z30.h, z0.h
        fdiv z31.h, p0/m, z31.h, z0.h
        subs x18, x18, 1
        bne loop
        ret


