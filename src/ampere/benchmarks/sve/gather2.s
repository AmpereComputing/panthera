//
// Copyright (c) 2023-2025 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
        .align 8
        .arch armv8.6-a+fp16+sve2
data1:
        .4byte 1,2,3,4,0,0,0,0,  1,2,3,4,0,0,0,0
data2:
        .4byte 1,2,3,4,0,0,0,0,  1,2,3,4,0,0,0,0
data3:
        .4byte 1,2,3,4,0,0,0,0,  1,2,3,4,0,0,0,0
data4:
        .4byte 1,2,3,4,0,0,0,0,  1,2,3,4,0,0,0,0
indx0:
        .4byte 0,1,2,3
indx1:
        .4byte 3,1,0,2
indx2:
        .4byte 2,3,1,0
indx3:
        .4byte 1,0,3,2

    .align 8
data1d:
        .8byte 1,2,3,4,0,0,0,0
data2d:        
    .8byte 1,2,3,4,0,0,0,0
data3d:        
    .8byte 1,2,3,4,0,0,0,0
data4d:        
    .8byte 1,2,3,4,0,0,0,0
indx0d:
        .8byte 0,1
indx1d:
        .8byte 3,1
indx2d:
        .8byte 2,3
indx3d:
        .8byte 1,0


        .align 8
        .arch armv8.6-a+fp16+sve2
        .global gather4
gather4:
        mov   x18, 0x1000
        ptrue p3.S
        adr   x0, indx0
        ldr   z31, [x0]
        adr   x0, indx1
        ldr   z30, [x0]
        adr   x0, indx2
        ldr   z29, [x0]
        adr   x0, indx3
        ldr   z28, [x0]
        adr   x0, data1
    adr   x1, data2
    adr   x2, data3
    adr   x3, data4
loop4:
        ld1w  {z0.s}, p3/z, [x0, z28.s, uxtw]
        ld1w  {z1.s}, p3/z, [x1, z29.s, uxtw]
        ld1w  {z2.s}, p3/z, [x2, z30.s, uxtw]
        ld1w  {z3.s}, p3/z, [x3, z31.s, uxtw]
        subs  x18, x18, 1
        bne   loop4
        ret
    
        .global gather2
gather2:
        mov   x18, 0x1000
        ptrue p3.D
        adr   x0, indx0d
        ldr   z31, [x0]
        adr   x0, indx1d
        ldr   z30, [x0]
        adr   x0, indx2d
        ldr   z29, [x0]
        adr   x0, indx3d
        ldr   z28, [x0]
        adr   x0, data1d
    adr   x1, data2d
    adr   x2, data3d
    adr   x3, data4d
loop2:
        ld1d  {z0.d}, p3/z, [x0, z28.d]
        ld1d  {z1.d}, p3/z, [x1, z29.d]
        ld1d  {z2.d}, p3/z, [x2, z30.d]
        ld1d  {z3.d}, p3/z, [x3, z31.d]
        subs  x18, x18, 1
        bne   loop2
        ret
        
