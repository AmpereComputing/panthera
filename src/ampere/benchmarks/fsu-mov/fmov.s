//
// Copyright (c) 2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
        .global fmov
fmov:        
        mov x18, 0x100
loop:
        fmov x17, d17         /* f2i */
        add  x17, x17, x17  
        fmov d17, x17         /* i2f */
        
        fmov x17, d17         /* f2i */
        add  x17, x17, x17  
        fmov d17, x17         /* i2f */
        
        fmov x17, d17         /* f2i */
        add  x17, x17, x17  
        fmov d17, x17         /* i2f */
        
        fmov x17, d17         /* f2i */
        add  x17, x17, x17  
        fmov d17, x17         /* i2f */


        
        fmov x17, d17         /* f2i */
        add  x17, x17, x17  
        fmov d17, x17         /* i2f */
        
        fmov x17, d17         /* f2i */
        add  x17, x17, x17  
        fmov d17, x17         /* i2f */
        
        fmov x17, d17         /* f2i */
        add  x17, x17, x17  
        fmov d17, x17         /* i2f */
        
        fmov x17, d17         /* f2i */
        add  x17, x17, x17  
        fmov d17, x17         /* i2f */


        
        fmov x17, d17         /* f2i */
        add  x17, x17, x17  
        fmov d17, x17         /* i2f */
        
        fmov x17, d17         /* f2i */
        add  x17, x17, x17  
        fmov d17, x17         /* i2f */
        
        subs x18, x18, 1
        bne loop
        ret


