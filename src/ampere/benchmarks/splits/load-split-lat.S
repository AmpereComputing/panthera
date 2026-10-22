//
// Copyright (c) 2024 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

.globl load_split_lat

.data
.align 16
load_data:
        .space 65536, 0

.text
.align 6
load_split_lat:
    mov  x13, x0

        // Set up base address in x9 to be 4 bytes away from the end of a cache line
        adrp x9, load_data
        add  x9, x9, #:lo12:load_data
        add  x9, x9, #60

        // 16 consecutive dependent line splits
.Lsplit_loop:
        ldr x10, [x9]
        add x9, x9, x10
        ldr x10, [x9, #0x40]
        add x9, x9, x10
        ldr x10, [x9, #0x80]
        add x9, x9, x10
        ldr x10, [x9, #0xc0]
        add x9, x9, x10

        ldr x10, [x9, #0x100]
        add x9, x9, x10
        ldr x10, [x9, #0x140]
        add x9, x9, x10
        ldr x10, [x9, #0x180]
        add x9, x9, x10
        ldr x10, [x9, #0x1c0]
        add x9, x9, x10

        ldr x10, [x9, #0x200]
        add x9, x9, x10
        ldr x10, [x9, #0x240]
        add x9, x9, x10
        ldr x10, [x9, #0x280]
        add x9, x9, x10
        ldr x10, [x9, #0x2c0]
        add x9, x9, x10

        ldr x10, [x9, #0x300]
        add x9, x9, x10
        ldr x10, [x9, #0x340]
        add x9, x9, x10
        ldr x10, [x9, #0x380]
        add x9, x9, x10
        ldr x10, [x9, #0x3c0]
        add x9, x9, x10

    subs x13,x13, #1
    bne .Lsplit_loop
    ret
