//
// Copyright (c) 2024 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
         .align 8
numer:
        .short 0x0000
        .short 0x0000
        .short 0x0000
        .short 0x0000
        .short 0x0000
        .short 0x0000
        .short 0x0000
        .short 0x0000


denom:
        .short 0x4200
        .short 0x4200
        .short 0x4200
        .short 0x4200
        .short 0x4200
        .short 0x4200
        .short 0x4200
        .short 0x4200

        .global dependent_sve_fdiv16_earlyexit
dependent_sve_fdiv16_earlyexit:
        mov x18, 0x100
        ldr  q10, numer
        ldr  q31, denom
        ptrue p0.h
        mov z0.d, z10.d
loop:
        fdiv z0.h, p0/m, z0.h, z31.h
        fdiv z0.h, p0/m, z0.h, z31.h
        fdiv z0.h, p0/m, z0.h, z31.h
        fdiv z0.h, p0/m, z0.h, z31.h

        fdiv z0.h, p0/m, z0.h, z31.h
        fdiv z0.h, p0/m, z0.h, z31.h
        fdiv z0.h, p0/m, z0.h, z31.h
        fdiv z0.h, p0/m, z0.h, z31.h

        fdiv z0.h, p0/m, z0.h, z31.h
        fdiv z0.h, p0/m, z0.h, z31.h
        fdiv z0.h, p0/m, z0.h, z31.h
        fdiv z0.h, p0/m, z0.h, z31.h

        fdiv z0.h, p0/m, z0.h, z31.h
        fdiv z0.h, p0/m, z0.h, z31.h
        fdiv z0.h, p0/m, z0.h, z31.h
        fdiv z0.h, p0/m, z0.h, z31.h

        fdiv z0.h, p0/m, z0.h, z31.h
        fdiv z0.h, p0/m, z0.h, z31.h
        fdiv z0.h, p0/m, z0.h, z31.h
        fdiv z0.h, p0/m, z0.h, z31.h

        fdiv z0.h, p0/m, z0.h, z31.h
        fdiv z0.h, p0/m, z0.h, z31.h
        fdiv z0.h, p0/m, z0.h, z31.h
        fdiv z0.h, p0/m, z0.h, z31.h

        fdiv z0.h, p0/m, z0.h, z31.h
        fdiv z0.h, p0/m, z0.h, z31.h
        fdiv z0.h, p0/m, z0.h, z31.h
        fdiv z0.h, p0/m, z0.h, z31.h

        subs x18, x18, 1
        bne loop
        ret


