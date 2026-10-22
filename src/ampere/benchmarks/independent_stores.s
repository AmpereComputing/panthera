//
// Copyright (c) 2019-2025 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
.data
.align 8
store_data:
    .space 1024, 0

.text
independent_stores:
    mov x18, 0x1000
    mov x10, 54321

    adrp x8, store_data
    add  x8, x8, #:lo12:store_data

loop:
    str x10, [x8]
    str x10, [x8, 8]
    str x10, [x8, 16]
    str x10, [x8, 24]
    str x10, [x8, 32]
    str x10, [x8, 40]
    str x10, [x8, 48]
    str x10, [x8, 56]

    subs x18, x18, 1
    bne loop
    ret

.global independent_stores
