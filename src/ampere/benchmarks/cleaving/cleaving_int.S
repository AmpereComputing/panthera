//
// Copyright (c) 2024 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

.globl cleaving_int_A
.globl cleaving_int_B
.globl cleaving_int_C
.globl cleaving_int_glass_jaw
.globl cleaving_int_glass_jaw2

.data
.align 16
load_data:
        .space 1048576, 0

.text
.align 6
cleaving_int_A:
    mov  x13, x0

        // Minimal loop that should achieve 8.0 IPC purely with fusion
.Lcleaving_loop_A:
        add x2, x1, #1
        nop
        add x2, x1, #1
        nop
        add x2, x1, #1
        nop

    subs x13,x13, #1
    bne .Lcleaving_loop_A

    ret


.align 6
cleaving_int_B:
    mov  x13, x0
        adrp x9, load_data
        add  x9, x9, #:lo12:load_data

        // More cleaving, less fusion, still 8.0 IPC
.Lcleaving_loop_B:
        ldr x2, [x9]    // these two cleave -- first load, first integer
        add x2, x1, #1

        ldr x2, [x9, #0x8]  // these two cleave -- second load, second integer
        add x2, x1, #1

        str x3, [x9, #0x10] // these two cleave -- first store, third integer
        add x2, x1, #1

    subs x13,x13, #1      // these two fuse -- fourth integer
    bne .Lcleaving_loop_B
    ret



.align 6
cleaving_int_C:
    mov  x13, x0
        adrp x9, load_data
        add  x9, x9, #:lo12:load_data
        add x8, x9, #0x40
        add x7, x9, #0x80
        add x6, x9, #0xc0
        add x5, x9, #0x100
        add x4, x9, #0x140
        add x3, x9, #0x180
        add x2, x9, #0x1c0

        // Try some internal cleaving. This loop is 17 instructions. Without cleaving, the post-increment
        // loads and stores and the FP store are 2 uops each, so this loop decodes to 24 uops, which takes
        // 6 cycles to dispatch, so the IPC is 17/6 = 2.83. With cleaving, all the 2-uop instructions become
        // 1 uop, so the loop is 16 uops = 4 cycles to dispatch = IPC of 17/4 = 4.25.
.Lcleaving_loop_C:
        ldr x1, [x9], #8    // post-increment, load+add cleave
        sub x9, x9, #8
        str x0, [x8], #8    // post-increment, SUS+add cleave
        sub x8, x8, #8

        ldr x1, [x7], #8    // post-increment, load+add cleave
        sub x7, x7, #8
        ldr x1, [x6], #8    // post-increment, load+add cleave
        sub x6, x6, #8

        ldr x1, [x5], #8    // post-increment, load+add cleave
        sub x5, x5, #8
        ldr x1, [x4], #8    // post-increment, load+add cleave
        sub x4, x4, #8

        ldr x1, [x3], #8    // post-increment, load+add cleave
        sub x3, x3, #8
        str d0, [x8]        // STA + STDV cleave

    subs x13,x13, #1    // fuse
    bne .Lcleaving_loop_C
    ret


.align 6
cleaving_int_glass_jaw:
    mov  x13, x0

        // This loop illustrates a glass jaw created by cleaving. The 8 adds all cleave, which means that
        // one half goes to pipe A and the other to pipe B. The subs/branch fuse into a single A-only uop.
        // Since pipe assignment has no flexibility at all on where to send things, we send more uops to A
        // than to B, and execute less than 4 uops/cycle. Without cleaving all the adds are AB uops, and
        // pipe assignment can send them to B to compensate for the A-only branch.
.Lcleaving_loop_glass_jaw:
        add x2, x1, #1
        add x2, x1, #1
        add x2, x1, #1
        add x2, x1, #1
        add x2, x1, #1
        add x2, x1, #1
        add x2, x1, #1
        add x2, x1, #1

    subs x13,x13, #1
    bne .Lcleaving_loop_glass_jaw
    ret


.text
.align 6
cleaving_int_glass_jaw2:
    mov  x13, x0

        // This loop shows that giving priority to fusion is not always the best choice. We fuse nops to adds
        // in two places, leading the middle add and the final nop to go alone, so this loop decodes to 5 uops.
        // If we had cleaved the two adds and fused the two nops, we could decode to 4 uops and hit 8.0 IPC.
.Lcleaving_loop_glass_jaw2:
        add x2, x1, #1  // nop fuses to the add
        nop
        add x2, x1, #1
        add x2, x1, #1  // nop fuses to the add
        nop
        nop

    subs x13,x13, #1
    bne .Lcleaving_loop_glass_jaw2

    ret


