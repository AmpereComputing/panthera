//
// Copyright (c) 2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
        .global smov
smov:        
        mov x18, 0x100
loop:
        smov x17, v1.b[0]    /* f2i */
        add  x17, x17, x17  
        fmov S1, w17         /* i2f */

        smov x17, v1.b[0]    /* f2i */
        add  x17, x17, x17  
        fmov S1, w17         /* i2f */

        smov x17, v1.b[0]    /* f2i */
        add  x17, x17, x17  
        fmov S1, w17         /* i2f */

        smov x17, v1.b[0]    /* f2i */
        add  x17, x17, x17  
        fmov S1, w17         /* i2f */


        smov x17, v1.b[0]    /* f2i */
        add  x17, x17, x17  
        fmov S1, w17         /* i2f */

        smov x17, v1.b[0]    /* f2i */
        add  x17, x17, x17  
        fmov S1, w17         /* i2f */

        smov x17, v1.b[0]    /* f2i */
        add  x17, x17, x17  
        fmov S1, w17         /* i2f */

        smov x17, v1.b[0]    /* f2i */
        add  x17, x17, x17  
        fmov S1, w17         /* i2f */


        
        smov x17, v1.b[0]    /* f2i */
        add  x17, x17, x17  
        fmov S1, w17         /* i2f */

        smov x17, v1.b[0]    /* f2i */
        add  x17, x17, x17  
        fmov S1, w17         /* i2f */


        subs x18, x18, 1
        bne loop
        ret


