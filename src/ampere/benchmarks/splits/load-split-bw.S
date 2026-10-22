//
// Copyright (c) 2024 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

.globl load_split_bw

.data
.align 16
load_data:
        .space 65536, 0

.text
.align 6
load_split_bw:
    mov  x13, x0

        // Set up base address in x9 to be 4 bytes away from the end of a cache line
        adrp x9, load_data
        add  x9, x9, #:lo12:load_data
        add  x9, x9, #60

        // 16 consecutive independent line splits
.Lsplit_loop:
        ldr x10, [x9]
        ldr x10, [x9, #0x40]
        ldr x10, [x9, #0x80]
        ldr x10, [x9, #0xc0]

        ldr x10, [x9, #0x100]
        ldr x10, [x9, #0x140]
        ldr x10, [x9, #0x180]
        ldr x10, [x9, #0x1c0]

        ldr x10, [x9, #0x200]
        ldr x10, [x9, #0x240]
        ldr x10, [x9, #0x280]
        ldr x10, [x9, #0x2c0]

        ldr x10, [x9, #0x300]
        ldr x10, [x9, #0x340]
        ldr x10, [x9, #0x380]
        ldr x10, [x9, #0x3c0]

    subs x13,x13, #1
    bne .Lsplit_loop
    ret
