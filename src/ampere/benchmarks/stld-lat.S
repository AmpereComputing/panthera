//
// Copyright (c) 2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

.globl stld_lat
.globl stld_lat_stpad
.globl stld_lat_stpad_delay_addr
.globl together_parallel_load_addr
.globl data_after_1
.globl data_after_1_parallel_load_addr
.globl data_after_2
.globl data_after_2_parallel_load_addr
.globl data_after_3
.globl data_after_3_parallel_load_addr
.globl data_after_4
.globl data_after_4_parallel_load_addr
.globl data_after_5
.globl data_after_5_parallel_load_addr
.globl data_after_6
.globl data_after_6_parallel_load_addr

.data
data_location_A:
.quad 0x0
data_location_B:
.quad 0x0
data_location_C:
.quad 0x0
data_location_D:
.quad 0x0

.align 6
.text
stld_lat:
    mov  x7, x0

    adrp x12, data_location_A
    add x8, x12, :lo12:data_location_A

    adrp x12, data_location_B
    add x9, x12, :lo12:data_location_B

    adrp x12, data_location_C
    add x10, x12, :lo12:data_location_C

    adrp x12, data_location_D
    add x11, x12, :lo12:data_location_D

    movz x12, #1

.Lroundtrip_stld:

    // Store value in x12 to location A
    str x12, [x8]
    // Create st-ld fwd opportunity by loading A into x13
    ldr x13, [x8]

    // Continue dependence chain via x13 into B
    str x13, [x9]
    // Create st-ld fwd opportunity by loading B into x14
    ldr x14, [x9]

    sub x7,x7, #1

    // Continue dependence chain via x14 into C
    str x14, [x10]
    // Create st-ld fwd opportunity by loading C into x15
    ldr x15, [x10]

    // Continue dependence chain via x15 into D
    str x15, [x11]
    // Create st-ld fwd opportunity by loading D into x12
    // ensure we load into 12 so that the store at the
    // top of the loop continues the dependency across
    // the loop iterations
    ldr x12, [x11]

    cbnz x7, .Lroundtrip_stld
    ret

.align 6
.text
stld_lat_stpad:
    mov  x7, x0

    adrp x12, data_location_A
    add x8, x12, :lo12:data_location_A

    adrp x12, data_location_B
    add x9, x12, :lo12:data_location_B

    adrp x12, data_location_C
    add x10, x12, :lo12:data_location_C

    adrp x12, data_location_D
    add x11, x12, :lo12:data_location_D

    movz x12, #1

.Lroundtrip_stld_stpad:

    // Store value in x12 to location A
    str x12, [x8]
    // Create st-ld fwd opportunity by loading A into x13
    ldr x13, [x8]

    // Pad load-store dependency to inhibit load-dependency
    // speculative wakeup impact
    add x13, x13, xzr
    add x13, x13, xzr
    add x13, x13, xzr
    add x13, x13, xzr
    add x13, x13, xzr
    add x13, x13, xzr
    add x13, x13, xzr
    add x13, x13, xzr
    add x13, x13, xzr
    add x13, x13, xzr
    add x13, x13, xzr
    add x13, x13, xzr
    add x13, x13, xzr
    add x13, x13, xzr
    add x13, x13, xzr
    add x13, x13, xzr

    // Continue dependence chain via x13 into B
    str x13, [x9]
    // Create st-ld fwd opportunity by loading B into x14
    ldr x14, [x9]

    sub x7,x7, #1

    // Pad load-store dependency to inhibit load-dependency
    // speculative wakeup impact
    add x14, x14, xzr
    add x14, x14, xzr
    add x14, x14, xzr
    add x14, x14, xzr
    add x14, x14, xzr
    add x14, x14, xzr
    add x14, x14, xzr
    add x14, x14, xzr
    add x14, x14, xzr
    add x14, x14, xzr
    add x14, x14, xzr
    add x14, x14, xzr
    add x14, x14, xzr
    add x14, x14, xzr
    add x14, x14, xzr
    add x14, x14, xzr

    // Continue dependence chain via x14 into C
    str x14, [x10]
    // Create st-ld fwd opportunity by loading C into x15
    ldr x15, [x10]

    // Pad load-store dependency to inhibit load-dependency
    // speculative wakeup impact
    add x15, x15, xzr
    add x15, x15, xzr
    add x15, x15, xzr
    add x15, x15, xzr
    add x15, x15, xzr
    add x15, x15, xzr
    add x15, x15, xzr
    add x15, x15, xzr
    add x15, x15, xzr
    add x15, x15, xzr
    add x15, x15, xzr
    add x15, x15, xzr
    add x15, x15, xzr
    add x15, x15, xzr
    add x15, x15, xzr
    add x15, x15, xzr

    // Continue dependence chain via x15 into D
    str x15, [x11]
    // Create st-ld fwd opportunity by loading D into x12
    // ensure we load into 12 so that the store at the
    // top of the loop continues the dependency across
    // the loop iterations
    ldr x12, [x11]

    // Pad load-store dependency to inhibit load-dependency
    // speculative wakeup impact
    add x12, x12, xzr
    add x12, x12, xzr
    add x12, x12, xzr
    add x12, x12, xzr
    add x12, x12, xzr
    add x12, x12, xzr
    add x12, x12, xzr
    add x12, x12, xzr
    add x12, x12, xzr
    add x12, x12, xzr
    add x12, x12, xzr
    add x12, x12, xzr
    add x12, x12, xzr
    add x12, x12, xzr
    add x12, x12, xzr
    add x12, x12, xzr

    cbnz x7, .Lroundtrip_stld_stpad
    ret

.align 6
.text
stld_lat_stpad_delay_addr:
    mov  x7, x0

    adrp x12, data_location_A
    add x8, x12, :lo12:data_location_A

    adrp x12, data_location_B
    add x9, x12, :lo12:data_location_B

    adrp x12, data_location_C
    add x10, x12, :lo12:data_location_C

    adrp x12, data_location_D
    add x11, x12, :lo12:data_location_D

    // Use zero-valued data for this test to make creation of a non-effectual dependency to delay address availability
    mov x12, xzr

.Lroundtrip_stld_stpad_delay_addr:

    // Store value in x12 to location A
    str x12, [x8]
    // Create st-ld fwd opportunity by loading A into x13
    ldr x13, [x8]

    // Pad load-store dependency to inhibit load-dependency
    // speculative wakeup impact
    add x13, x13, xzr
    add x13, x13, xzr
    add x13, x13, xzr
    add x13, x13, xzr
    add x13, x13, xzr
    add x13, x13, xzr
    add x13, x13, xzr
    add x13, x13, xzr
    add x13, x13, xzr
    add x13, x13, xzr
    add x13, x13, xzr
    add x13, x13, xzr
    add x13, x13, xzr
    add x13, x13, xzr
    add x13, x13, xzr
    add x13, x13, xzr

    // Delay availability of store address
    add x9, x9, x13

    // Continue dependence chain via x13 into B
    str x13, [x9]
    // Create st-ld fwd opportunity by loading B into x14
    ldr x14, [x9]

    sub x7,x7, #1

    // Pad load-store dependency to inhibit load-dependency
    // speculative wakeup impact
    add x14, x14, xzr
    add x14, x14, xzr
    add x14, x14, xzr
    add x14, x14, xzr
    add x14, x14, xzr
    add x14, x14, xzr
    add x14, x14, xzr
    add x14, x14, xzr
    add x14, x14, xzr
    add x14, x14, xzr
    add x14, x14, xzr
    add x14, x14, xzr
    add x14, x14, xzr
    add x14, x14, xzr
    add x14, x14, xzr
    add x14, x14, xzr

    // Delay availability of store address
    add x10, x10, x14

    // Continue dependence chain via x14 into C
    str x14, [x10]
    // Create st-ld fwd opportunity by loading C into x15
    ldr x15, [x10]

    // Pad load-store dependency to inhibit load-dependency
    // speculative wakeup impact
    add x15, x15, xzr
    add x15, x15, xzr
    add x15, x15, xzr
    add x15, x15, xzr
    add x15, x15, xzr
    add x15, x15, xzr
    add x15, x15, xzr
    add x15, x15, xzr
    add x15, x15, xzr
    add x15, x15, xzr
    add x15, x15, xzr
    add x15, x15, xzr
    add x15, x15, xzr
    add x15, x15, xzr
    add x15, x15, xzr
    add x15, x15, xzr

    // Delay availability of store address
    add x11, x11, x15

    // Continue dependence chain via x15 into D
    str x15, [x11]
    // Create st-ld fwd opportunity by loading D into x12
    // ensure we load into 12 so that the store at the
    // top of the loop continues the dependency across
    // the loop iterations
    ldr x12, [x11]

    // Pad load-store dependency to inhibit load-dependency
    // speculative wakeup impact
    add x12, x12, xzr
    add x12, x12, xzr
    add x12, x12, xzr
    add x12, x12, xzr
    add x12, x12, xzr
    add x12, x12, xzr
    add x12, x12, xzr
    add x12, x12, xzr
    add x12, x12, xzr
    add x12, x12, xzr
    add x12, x12, xzr
    add x12, x12, xzr
    add x12, x12, xzr
    add x12, x12, xzr
    add x12, x12, xzr
    add x12, x12, xzr

    // Delay availability of store address
    add x8, x8, x12

    cbnz x7, .Lroundtrip_stld_stpad_delay_addr
    ret


.align 6
.text
together_parallel_load_addr:
    mov  x7, x0

    adrp x12, data_location_A
    add x8, x12, :lo12:data_location_A
    mov x9, x8

    // Use zero-valued data for this test to make creation of a non-effectual dependency to delay address availability
    mov x12, xzr

.Lroundtrip_together_parallel_load_addr:

    // Store value in x12 to location A
    str x12, [x8]
    // Create st-ld fwd opportunity by loading A into x12
    // Load address is ready ahead of time
    ldr x12, [x9]

    // Pad load-store dependency to make the event stream easier to follow.
    // Use lsl because it can only go on B-pipe, so the add which generates the address for the next
    // iteration will reliably execute (on A-pipe) in the same cycle as the following lsl.
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr

    // Addr will be ready after data, store single-issues
    add x8, x8, x12

    sub x7,x7, #1
    cbnz x7, .Lroundtrip_together_parallel_load_addr
    ret


.align 6
.text
data_after_1:
    mov  x7, x0

    adrp x12, data_location_A
    add x8, x12, :lo12:data_location_A
    mov x9, x8

    // Use zero-valued data for this test to make creation of a non-effectual dependency to delay address availability
    mov x12, xzr

.Lroundtrip_data_after_1:

    // Store value in x12 to location A
    str x12, [x8]
    // Create st-ld fwd opportunity by loading A into x12
    ldr x12, [x8]

    // Pad load-store dependency to make the event stream easier to follow.
    // Use lsl because it can only go on B-pipe, so the add which generates the address for the next
    // iteration will reliably execute (on A-pipe) in the same cycle as the following lsl.
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr

    // Data will be ready one cycle after address
    add x8, x8, x12

    lsl x12, x12, xzr
    lsl x12, x12, xzr

    sub x7,x7, #1
    cbnz x7, .Lroundtrip_data_after_1
    ret


.align 6
.text
data_after_1_parallel_load_addr:
    mov  x7, x0

    adrp x12, data_location_A
    add x8, x12, :lo12:data_location_A
    mov x9, x8

    // Use zero-valued data for this test to make creation of a non-effectual dependency to delay address availability
    mov x12, xzr

.Lroundtrip_data_after_1_parallel_load_addr:

    // Store value in x12 to location A
    str x12, [x8]
    // Create st-ld fwd opportunity by loading A into x12
    // Load address is ready ahead of time
    ldr x12, [x9]

    // Pad load-store dependency to make the event stream easier to follow.
    // Use lsl because it can only go on B-pipe, so the add which generates the address for the next
    // iteration will reliably execute (on A-pipe) in the same cycle as the following lsl.
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr

    // Data will be ready one cycle after address
    add x8, x8, x12

    lsl x12, x12, xzr
    lsl x12, x12, xzr

    sub x7,x7, #1
    cbnz x7, .Lroundtrip_data_after_1_parallel_load_addr
    ret


.align 6
.text
data_after_2:
    mov  x7, x0

    adrp x12, data_location_A
    add x8, x12, :lo12:data_location_A
    mov x9, x8

    // Use zero-valued data for this test to make creation of a non-effectual dependency to delay address availability
    mov x12, xzr

.Lroundtrip_data_after_2:

    // Store value in x12 to location A
    str x12, [x8]
    // Create st-ld fwd opportunity by loading A into x12
    ldr x12, [x8]

    // Pad load-store dependency to make the event stream easier to follow.
    // Use lsl because it can only go on B-pipe, so the add which generates the address for the next
    // iteration will reliably execute (on A-pipe) in the same cycle as the following lsl.
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr

    // Data will be ready 2 cycles after address
    add x8, x8, x12

    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr

    sub x7,x7, #1
    cbnz x7, .Lroundtrip_data_after_2
    ret


.align 6
.text
data_after_2_parallel_load_addr:
    mov  x7, x0

    adrp x12, data_location_A
    add x8, x12, :lo12:data_location_A
    mov x9, x8

    // Use zero-valued data for this test to make creation of a non-effectual dependency to delay address availability
    mov x12, xzr

.Lroundtrip_data_after_2_parallel_load_addr:

    // Store value in x12 to location A
    str x12, [x8]
    // Create st-ld fwd opportunity by loading A into x12
    // Load address is ready ahead of time
    ldr x12, [x9]

    // Pad load-store dependency to make the event stream easier to follow.
    // Use lsl because it can only go on B-pipe, so the add which generates the address for the next
    // iteration will reliably execute (on A-pipe) in the same cycle as the following lsl.
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr

    // Data will be ready 2 cycles after address
    add x8, x8, x12

    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr

    sub x7,x7, #1
    cbnz x7, .Lroundtrip_data_after_2_parallel_load_addr
    ret


.align 6
.text
data_after_3:
    mov  x7, x0

    adrp x12, data_location_A
    add x8, x12, :lo12:data_location_A
    mov x9, x8

    // Use zero-valued data for this test to make creation of a non-effectual dependency to delay address availability
    mov x12, xzr

.Lroundtrip_data_after_3:

    // Store value in x12 to location A
    str x12, [x8]
    // Create st-ld fwd opportunity by loading A into x12
    ldr x12, [x8]

    // Pad load-store dependency to make the event stream easier to follow.
    // Use lsl because it can only go on B-pipe, so the add which generates the address for the next
    // iteration will reliably execute (on A-pipe) in the same cycle as the following lsl.
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr

    // Data will be ready 3 cycles after address
    add x8, x8, x12

    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr

    sub x7,x7, #1
    cbnz x7, .Lroundtrip_data_after_3
    ret


.align 6
.text
data_after_3_parallel_load_addr:
    mov  x7, x0

    adrp x12, data_location_A
    add x8, x12, :lo12:data_location_A
    mov x9, x8

    // Use zero-valued data for this test to make creation of a non-effectual dependency to delay address availability
    mov x12, xzr

.Lroundtrip_data_after_3_parallel_load_addr:

    // Store value in x12 to location A
    str x12, [x8]
    // Create st-ld fwd opportunity by loading A into x12
    // Load address is ready ahead of time
    ldr x12, [x9]

    // Pad load-store dependency to make the event stream easier to follow.
    // Use lsl because it can only go on B-pipe, so the add which generates the address for the next
    // iteration will reliably execute (on A-pipe) in the same cycle as the following lsl.
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr

    // Data will be ready 3 cycles after address
    add x8, x8, x12

    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr

    sub x7,x7, #1
    cbnz x7, .Lroundtrip_data_after_3_parallel_load_addr
    ret


.align 6
.text
data_after_4:
    mov  x7, x0

    adrp x12, data_location_A
    add x8, x12, :lo12:data_location_A
    mov x9, x8

    // Use zero-valued data for this test to make creation of a non-effectual dependency to delay address availability
    mov x12, xzr

.Lroundtrip_data_after_4:

    // Store value in x12 to location A
    str x12, [x8]
    // Create st-ld fwd opportunity by loading A into x12
    ldr x12, [x8]

    // Pad load-store dependency to make the event stream easier to follow.
    // Use lsl because it can only go on B-pipe, so the add which generates the address for the next
    // iteration will reliably execute (on A-pipe) in the same cycle as the following lsl.
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr

    // Data will be ready 4 cycles after address
    add x8, x8, x12

    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr

    sub x7,x7, #1
    cbnz x7, .Lroundtrip_data_after_4
    ret


.align 6
.text
data_after_4_parallel_load_addr:
    mov  x7, x0

    adrp x12, data_location_A
    add x8, x12, :lo12:data_location_A
    mov x9, x8

    // Use zero-valued data for this test to make creation of a non-effectual dependency to delay address availability
    mov x12, xzr

.Lroundtrip_data_after_4_parallel_load_addr:

    // Store value in x12 to location A
    str x12, [x8]
    // Create st-ld fwd opportunity by loading A into x12
    // Load address is ready ahead of time
    ldr x12, [x9]

    // Pad load-store dependency to make the event stream easier to follow.
    // Use lsl because it can only go on B-pipe, so the add which generates the address for the next
    // iteration will reliably execute (on A-pipe) in the same cycle as the following lsl.
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr

    // Data will be ready 4 cycles after address
    add x8, x8, x12

    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr

    sub x7,x7, #1
    cbnz x7, .Lroundtrip_data_after_4_parallel_load_addr
    ret


.align 6
.text
data_after_5:
    mov  x7, x0

    adrp x12, data_location_A
    add x8, x12, :lo12:data_location_A
    mov x9, x8

    // Use zero-valued data for this test to make creation of a non-effectual dependency to delay address availability
    mov x12, xzr

.Lroundtrip_data_after_5:

    // Store value in x12 to location A
    str x12, [x8]
    // Create st-ld fwd opportunity by loading A into x12
    ldr x12, [x8]

    // Pad load-store dependency to make the event stream easier to follow.
    // Use lsl because it can only go on B-pipe, so the add which generates the address for the next
    // iteration will reliably execute (on A-pipe) in the same cycle as the following lsl.
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr

    // Data will be ready 5 cycles after address
    add x8, x8, x12

    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr

    sub x7,x7, #1
    cbnz x7, .Lroundtrip_data_after_5
    ret


.align 6
.text
data_after_5_parallel_load_addr:
    mov  x7, x0

    adrp x12, data_location_A
    add x8, x12, :lo12:data_location_A
    mov x9, x8

    // Use zero-valued data for this test to make creation of a non-effectual dependency to delay address availability
    mov x12, xzr

.Lroundtrip_data_after_5_parallel_load_addr:

    // Store value in x12 to location A
    str x12, [x8]
    // Create st-ld fwd opportunity by loading A into x12
    // Load address is ready ahead of time
    ldr x12, [x9]

    // Pad load-store dependency to make the event stream easier to follow.
    // Use lsl because it can only go on B-pipe, so the add which generates the address for the next
    // iteration will reliably execute (on A-pipe) in the same cycle as the following lsl.
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr

    // Data will be ready 5 cycles after address
    add x8, x8, x12

    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr

    sub x7,x7, #1
    cbnz x7, .Lroundtrip_data_after_5_parallel_load_addr
    ret


.align 6
.text
data_after_6:
    mov  x7, x0

    adrp x12, data_location_A
    add x8, x12, :lo12:data_location_A
    mov x9, x8

    // Use zero-valued data for this test to make creation of a non-effectual dependency to delay address availability
    mov x12, xzr

.Lroundtrip_data_after_6:

    // Store value in x12 to location A
    str x12, [x8]
    // Create st-ld fwd opportunity by loading A into x12
    ldr x12, [x8]

    // Pad load-store dependency to make the event stream easier to follow.
    // Use lsl because it can only go on B-pipe, so the add which generates the address for the next
    // iteration will reliably execute (on A-pipe) in the same cycle as the following lsl.
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr

    // Data will be ready 6 cycles after address
    add x8, x8, x12

    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr

    sub x7,x7, #1
    cbnz x7, .Lroundtrip_data_after_6
    ret


.align 6
.text
data_after_6_parallel_load_addr:
    mov  x7, x0

    adrp x12, data_location_A
    add x8, x12, :lo12:data_location_A
    mov x9, x8

    // Use zero-valued data for this test to make creation of a non-effectual dependency to delay address availability
    mov x12, xzr

.Lroundtrip_data_after_6_parallel_load_addr:

    // Store value in x12 to location A
    str x12, [x8]
    // Create st-ld fwd opportunity by loading A into x12
    // Load address is ready ahead of time
    ldr x12, [x9]

    // Pad load-store dependency to make the event stream easier to follow.
    // Use lsl because it can only go on B-pipe, so the add which generates the address for the next
    // iteration will reliably execute (on A-pipe) in the same cycle as the following lsl.
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr

    // Data will be ready 6 cycles after address
    add x8, x8, x12

    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr
    lsl x12, x12, xzr

    sub x7,x7, #1
    cbnz x7, .Lroundtrip_data_after_6_parallel_load_addr
    ret


