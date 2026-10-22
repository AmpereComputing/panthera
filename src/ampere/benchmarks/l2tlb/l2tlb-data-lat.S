//
// Copyright (c) 2024 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

.globl l2tlb_data_lat

.data
.align 16
load_data:
        .space 524288, 0

.text
.align 6
l2tlb_data_lat:
    mov  x13, x0

        // Do one load to each of 128 pages in a loop, so that every load misses the L1TLB
        // Each load depends on the previous one to measure the latency.
.Ll2tlb_outer_loop:
        mov x14, #8
        adrp x9, load_data
        add  x9, x9, #:lo12:load_data

        // The inner loop touches 8 pages, so that the stride of each load is 32K. The PCP
        // can only track 14-bit strides (16K max), so that should prevent PCP from prefetching
        // any pages.
.Ll2tlb_inner_loop:
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
        bne .Ll2tlb_inner_loop

    subs x13,x13, #1
    bne .Ll2tlb_outer_loop
    ret
