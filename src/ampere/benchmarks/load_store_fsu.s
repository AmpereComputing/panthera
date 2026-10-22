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
load_store_fsu:
    mov x18, 0x4000

    adrp x9, load_data
    add  x9, x9, #:lo12:load_data

    adrp x8, store_data
    add  x8, x8, #:lo12:store_data

loop:
    ldr d10, [x9]
    str d10, [x8]

    ldr d10, [x9, 8]
    str d10, [x8, 8]

    ldr d11, [x9, 16]
    str d11, [x8, 16]

    ldr d12, [x9, 24]
    str d12, [x8, 24]

    ldr d13, [x9, 32]
    str d13, [x8, 32]

    ldr d14, [x9, 40]
    str d14, [x8, 40]

    ldr d15, [x9, 48]
    str d15, [x8, 48]

    ldr d16, [x9, 56]
    str d16, [x8, 56]

    subs x18, x18, 1
    bne loop
    ret

.global load_store_fsu
