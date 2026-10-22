//
// Copyright (c) 2020-2025 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
    .text
    .align 6
    .global rapid_call_ret
rapid_call_ret:
    mov x14, 0x4000
    mov x15, x30
    b loop

    .align 6
loop:    
    bl f
    nop
    nop
    nop
    bl f
    nop
    nop
    nop
    bl f
    subs x14, x14, 1
    bne loop
    mov x30, x15
    ret

    .align 6
f:
    ret
