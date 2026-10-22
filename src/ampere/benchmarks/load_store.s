//
// Copyright (c) 2019-2025 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
.data
.align 8
load_data:
    .space 1216, 0
store_data:
    .space 1216, 0

.text
load_store:
    mov x18, 0x4000

    adrp x9, load_data
    add  x9, x9, #:lo12:load_data

    adrp x8, store_data
    add  x8, x8, #:lo12:store_data

loop:
    ldr x10, [x9]
    str x10, [x8]

    ldr x10, [x9, 8]
    str x10, [x8, 8]

    ldr x11, [x9, 16]
    str x11, [x8, 16]

    ldr x12, [x9, 24]
    str x12, [x8, 24]

    ldr x13, [x9, 32]
    str x13, [x8, 32]

    ldr x14, [x9, 40]
    str x14, [x8, 40]

    ldr x15, [x9, 48]
    str x15, [x8, 48]

    ldr x16, [x9, 56]
    str x16, [x8, 56]

    subs x18, x18, 1
    bne loop
    ret

.global load_store
