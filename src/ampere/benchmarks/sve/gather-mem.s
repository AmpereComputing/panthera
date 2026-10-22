//
// Copyright (c) 2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

        .arch armv8.6-a+fp16+sve2
        .align 16
        .global data1
        .lcomm data1, 64*1024*2

        
        .align 6
indx1:
        .4byte 3*64+0*4,1*64+1*4,0*64+2*4,2*64+3*4
indx2:
        .4byte 3*64+4*4,1*64+5*4,0*64+6*4,2*64+7*4
indx3:
        .4byte 3*64+8*4,1*64+9*4,0*64+10*4,2*64+11*4
indx4:
        .4byte 3*64+12*4,1*64+13*4,0*64+14*4,2*64+15*4
indxd1:
        .8byte 1*64+0*8,0*64+1*8
indxd2:
        .8byte 1*64+2*8,0*64+3*8
indxd3:
        .8byte 1*64+4*8,0*64+5*8
indxd4:
        .8byte 1*64+6*8,0*64+7*8

        .text
        .align 8
        .global data_addr1
data_addr1:
        adr x0, data1
        ret
        
        .global gather4
gather4:

        ptrue p3.S
        adr   x0, indx1
        ldr   z31, [x0]
        adr   x0, indx2
        ldr   z30, [x0]
        adr   x0, indx3
        ldr   z29, [x0]
        adr   x0, indx4
        ldr   z28, [x0]
        mov   x15, 16
outer4:        
        adr   x0, data1
        add   x1, x0, 64*256
        add   x2, x0, 64*512
        add   x3, x0, 64*768
        mov   x14, 64
loop4:
        ld1w  {z0.s}, p3/z, [x0, z31.s, uxtw]
        ld1w  {z1.s}, p3/z, [x1, z30.s, uxtw]
        ld1w  {z2.s}, p3/z, [x2, z29.s, uxtw]
        ld1w  {z3.s}, p3/z, [x3, z28.s, uxtw]
        add   x0, x0, 64*4
        add   x1, x1, 64*4
        add   x2, x2, 64*4
        add   x3, x3, 64*4                
        subs  x14, x14, 1
        bne   loop4
        subs  x15, x15, 1
        bne   outer4
        ret
        
        .global gather2
gather2:
        ptrue p3.D
        adr   x0, indxd1
        ldr   z31, [x0]
        adr   x0, indxd2
        ldr   z30, [x0]
        adr   x0, indxd3
        ldr   z29, [x0]
        adr   x0, indxd4
        ldr   z28, [x0]
        mov   x15, 8
outer2:        
        adr   x0, data1
        add   x1, x0, 64*256
        add   x2, x0, 64*512
        add   x3, x0, 64*768
        mov   x14, 128

loop2:
        ld1d  {z0.d}, p3/z, [x0, z31.d]
        ld1d  {z1.d}, p3/z, [x1, z30.d]
        ld1d  {z2.d}, p3/z, [x2, z29.d]
        ld1d  {z3.d}, p3/z, [x3, z28.d]
        add   x0, x0, 64*2
        add   x1, x1, 64*2
        add   x2, x2, 64*2
        add   x3, x3, 64*2                
        subs  x14, x14, 1
        bne   loop2
        subs  x15, x15, 1
        bne   outer2
        ret
        
