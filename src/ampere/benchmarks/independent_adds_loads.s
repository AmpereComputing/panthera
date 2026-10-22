//
// Copyright (c) 2019-2025 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
.align 8
independent_loads_data:
    .space 512, 0

independent_adds_loads:
    mov x18, 0x1000
    mov x0, 7
    adr x9, independent_loads_data
loop:
    add x1, x0, x0
    add x2, x0, x0
    ldr x10, [x9]
    ldr x10, [x9, 8]
    
    add x3, x0, x0
    add x4, x0, x0
    ldr x11, [x9, 16]
    ldr x12, [x9, 24]
    
    add x5, x0, x0
    add x6, x0, x0
    ldr x13, [x9, 32]
    ldr x14, [x9, 40]
    
    add x7, x0, x0
    add x8, x0, x0
    ldr x15, [x9, 48]
    ldr x16, [x9, 56]
    
    subs x18, x18, 1
    bne loop
    ret
    
.global independent_adds_loads
