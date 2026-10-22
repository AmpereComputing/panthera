//
// Copyright (c) 2024 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

.globl slow_integer_a
.globl slow_integer_b

.data
.align 16
load_data:
        .space 65536, 0

.text
.align 6
slow_integer_a:
    mov  x13, x0

        // Stress the B schedulers with a mix of 3-cycle multiplies and 1-cycle B-only uops
        .Lslow_integer_a_loop:
.rept 16
        mul x7, x8, x9
        lsl x10, x11, x12
.endr

    subs x13,x13, #1
    bne .Lslow_integer_a_loop
    ret

        .text

.align 6
slow_integer_b:
    mov  x13, x0

        // 2:1 ratio of shifts to multiplies. A truly separate BS pipe would increase IPC
        // on this from 2 to 3.
        .Lslow_integer_b_loop:
.rept 16
        mul x7, x8, x9
        lsl x10, x11, x12
        lsl x10, x11, x12
.endr

    subs x13,x13, #1
    bne .Lslow_integer_b_loop
    ret
