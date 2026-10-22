//
// Copyright (c) 2024 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

.globl load_cancel_ixu
.globl load_cancel_fsu

.data
.align 16
load_data:
        .space 65536, 0

.text
.align 6
load_cancel_ixu:
    mov  x13, x0

        // Set up base address in x9 to be 4 bytes away from the end of a cache line
        // The split load is guaranteed to always recycle (and cancel) once
        adrp x9, load_data
        add  x9, x9, #:lo12:load_data
        add  x9, x9, #60
        mov  x15, #0

        .Lcancel_ixu_loop:
        ldr x10, [x9]
        orr x11, x10, x15
        eor x12, x11, x15
        and x14, x12, x15
        add x9, x9, x14

    subs x13,x13, #1
    bne .Lcancel_ixu_loop
    ret


.text
.align 6
load_cancel_fsu:
    mov  x13, x0

        // Set up base address in x9 to be 4 bytes away from the end of a cache line
        // The split load is guaranteed to always recycle (and cancel) once
        adrp x9, load_data
        add  x9, x9, #:lo12:load_data
        add  x9, x9, #60
        mov  x15, #0

        .Lcancel_fsu_loop:
        ldr d10, [x9]
        fmov d11, d10
        fmov d12, d11
        fmov d13, d12
        fmov x10, d13
        and x10, x10, xzr
        add x9, x9, x10

    subs x13,x13, #1
    bne .Lcancel_fsu_loop
    ret
