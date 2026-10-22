//
// Copyright (c) 2022-2024 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
         .align 8
numer:
        .short 0x7bff
        .short 0x7bff
        .short 0x7bff
        .short 0x7bff
        .short 0x7bff
        .short 0x7bff
        .short 0x7bff
        .short 0x7bff

denom:
        .short 0x4200
        .short 0x4200
        .short 0x4200
        .short 0x4200
        .short 0x4200
        .short 0x4200
        .short 0x4200
        .short 0x4200

        .global dependent_sve_fdiv16
dependent_sve_fdiv16:
        mov x18, 0x100
        ldr  q10, numer
        ldr  q31, denom
        ptrue p0.h
loop:
        /*  reload numer */
        mov z0.d, z10.d

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

        eor z0.d, z0.d, z0.d  /* zero z0 */
        add z10.d, z10.d, z0.d /* create a dependence for next iteration*/

        subs x18, x18, 1
        bne loop
        ret


