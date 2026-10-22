//
// Copyright (c) 2019-2025 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
.align 8
independent_loads_data:
    .space 1024, 0

independent_loads:
    mov x18, 0x1000
    adr x9, independent_loads_data
loop:
    ldr x10, [x9]
    ldr x10, [x9, 8]
    ldr x11, [x9, 16]
    ldr x12, [x9, 24]
    ldr x13, [x9, 32]
    ldr x14, [x9, 40]
    ldr x15, [x9, 48]
    ldr x16, [x9, 56]

    subs x18, x18, 1
    bne loop
    ret


.global independent_loads
