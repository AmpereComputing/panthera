//
// Copyright (c) 2024 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
         .align 8
numer:
        .double 0
        .double 0
denom:
        .double 3.0
        .double 3.0

        .global dependent_sve_fdiv64_earlyexit
dependent_sve_fdiv64_earlyexit:
        mov x18, 0x100
        ldr  q31, denom
        ldr  q10, numer
        ptrue p0.d
        mov z0.d, z10.d
loop:
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

        subs x18, x18, 1
        bne loop
        ret


