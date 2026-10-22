//
// Copyright (c) 2024 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
         .align 8
numer:
        .float 0
        .float 0
        .float 0
        .float 0
denom:
        .float 3.0
        .float 3.0
        .float 3.0
        .float 3.0

        .global dependent_sve_fdiv32_earlyexit
dependent_sve_fdiv32_earlyexit:
        mov x18, 0x100
        ldr q31, denom
        ldr q10, numer
        ptrue p0.s
        mov z0.d, z10.d
loop:

        fdiv z0.s, p0/m, z0.s, z31.s
        fdiv z0.s, p0/m, z0.s, z31.s
        fdiv z0.s, p0/m, z0.s, z31.s
        fdiv z0.s, p0/m, z0.s, z31.s

        fdiv z0.s, p0/m, z0.s, z31.s
        fdiv z0.s, p0/m, z0.s, z31.s
        fdiv z0.s, p0/m, z0.s, z31.s
        fdiv z0.s, p0/m, z0.s, z31.s

        fdiv z0.s, p0/m, z0.s, z31.s
        fdiv z0.s, p0/m, z0.s, z31.s
        fdiv z0.s, p0/m, z0.s, z31.s
        fdiv z0.s, p0/m, z0.s, z31.s

        fdiv z0.s, p0/m, z0.s, z31.s
        fdiv z0.s, p0/m, z0.s, z31.s
        fdiv z0.s, p0/m, z0.s, z31.s
        fdiv z0.s, p0/m, z0.s, z31.s

        fdiv z0.s, p0/m, z0.s, z31.s
        fdiv z0.s, p0/m, z0.s, z31.s
        fdiv z0.s, p0/m, z0.s, z31.s
        fdiv z0.s, p0/m, z0.s, z31.s

        fdiv z0.s, p0/m, z0.s, z31.s
        fdiv z0.s, p0/m, z0.s, z31.s
        fdiv z0.s, p0/m, z0.s, z31.s
        fdiv z0.s, p0/m, z0.s, z31.s

        fdiv z0.s, p0/m, z0.s, z31.s
        fdiv z0.s, p0/m, z0.s, z31.s
        fdiv z0.s, p0/m, z0.s, z31.s
        fdiv z0.s, p0/m, z0.s, z31.s

        fdiv z0.s, p0/m, z0.s, z31.s
        fdiv z0.s, p0/m, z0.s, z31.s
        fdiv z0.s, p0/m, z0.s, z31.s
        fdiv z0.s, p0/m, z0.s, z31.s

        fdiv z0.s, p0/m, z0.s, z31.s
        fdiv z0.s, p0/m, z0.s, z31.s
        fdiv z0.s, p0/m, z0.s, z31.s
        fdiv z0.s, p0/m, z0.s, z31.s
        
        subs x18, x18, 1
        bne loop
        ret


