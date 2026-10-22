//
// Copyright (c) 2024 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

.globl l1_thrash_lat

.data
.align 16
load_data:
        .space 524288, 0

.text
.align 6
l1_thrash_lat:
    mov  x13, x0

        // This test attempted to test L2TLB hits, but failed in multiple ways. The loads all hit
        // in the L1TLB, but because the addresses have a 4K stride they thrash one L1 set,
        // and all the misses go to the same L2 bank. It ended up having some accidentally
        // interesting behavior, so we'll keep it around.
.Ll1_outer_loop:
        mov x14, #8
        adrp x9, load_data
        add  x9, x9, #:lo12:load_data

.Ll1_inner_loop:
        ldr x10, [x9]
        add x9, x9, x10
        ldr x10, [x9, #0x1000]
        add x9, x9, x10
        ldr x10, [x9, #0x2000]
        add x9, x9, x10
        ldr x10, [x9, #0x3000]
        add x9, x9, x10
        ldr x10, [x9, #0x4000]
        add x9, x9, x10
        ldr x10, [x9, #0x5000]
        add x9, x9, x10
        ldr x10, [x9, #0x6000]
        add x9, x9, x10
        ldr x10, [x9, #0x7000]
        add x9, x9, x10

        add x9, x9, #0x8000

    subs x14,x14, #1
        bne .Ll1_inner_loop

    subs x13,x13, #1
    bne .Ll1_outer_loop
    ret
