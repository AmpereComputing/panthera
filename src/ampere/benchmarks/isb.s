//
// Copyright (c) 2020-2025 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
.align 8
isb_test:
    mov x18, 0x1000
    mov x0, 7
loop:
    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0

    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0

    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0

    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0


    isb
    
    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0

    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0

    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0

    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0
    add x0, x0, x0

    subs x18, x18, 1
    bne loop
    ret
    
.global isb_test
