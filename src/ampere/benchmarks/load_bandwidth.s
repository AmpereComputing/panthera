//
// Copyright (c) 2019-2025 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
.align 8

load_bandwidth:
        // X0: address of data region
        // X1: size of data region in bytes (must be at least 2k)
        // X2: number of loops over data region
    lsr x14, x1, #11
    mov x4, x0
        add x5, x4, #512

        sub x2, x2, #1

loop:
    subs x14, x14, #1

    ldr x6, [x4]
    ldr x7, [x4, #72]
    ldr x8, [x4, #144]
    ldr x9, [x4, #216]

    ldr x10, [x4, #288]
    ldr x11, [x4, #360]
    ldr x12, [x4, #432]
    ldr x13, [x4, #504]

    add x4, x4, #1024

    ldr x6, [x5]
    ldr x7, [x5, #72]
    ldr x8, [x5, #144]
    ldr x9, [x5, #216]

    ldr x10, [x5, #288]
    ldr x11, [x5, #360]
    ldr x12, [x5, #432]
    ldr x13, [x5, #504]

    add x5, x5, #1024

    ldr x6, [x4]
    ldr x7, [x4, #72]
    ldr x8, [x4, #144]
    ldr x9, [x4, #216]

    ldr x10, [x4, #288]
    ldr x11, [x4, #360]
    ldr x12, [x4, #432]
    ldr x13, [x4, #504]

    add x4, x4, #1024

    ldr x6, [x5]
    ldr x7, [x5, #72]
    ldr x8, [x5, #144]
    ldr x9, [x5, #216]

    ldr x10, [x5, #288]
    ldr x11, [x5, #360]
    ldr x12, [x5, #432]
    ldr x13, [x5, #504]

    add x5, x5, #1024

    bne loop

        cbnz x2, load_bandwidth

    ret

.global load_bandwidth
