//
// Copyright (c) 2025 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

.globl loads_1b
.globl loads_4b
.globl loads_8b
.globl loads_16b
.globl loads_64b

.globl stores_1b
.globl stores_4b
.globl stores_8b
.globl stores_16b
.globl stores_64b

.data
.align 16
data_buffer:
        .space 8388608, 0

.text
.align 6
loads_1b:
        mov x12, #10

.Lloads_1b_outer:
        mov  x13, x0  // Loop count from env var BENCHMARK_ITERATIONS
        adrp x9, data_buffer
        add  x9, x9, #:lo12:data_buffer

.Lloads_1b_inner:
        ldrb w1, [x9]
        ldrb w1, [x9, #1]
        ldrb w1, [x9, #2]
        ldrb w1, [x9, #3]
        ldrb w1, [x9, #4]
        ldrb w1, [x9, #5]
        ldrb w1, [x9, #6]
        ldrb w1, [x9, #7]
        ldrb w1, [x9, #8]
        ldrb w1, [x9, #9]
        ldrb w1, [x9, #10]
        ldrb w1, [x9, #11]
        ldrb w1, [x9, #12]
        ldrb w1, [x9, #13]
        ldrb w1, [x9, #14]
        ldrb w1, [x9, #15]
        add x9, x9, #16

        subs x13,x13, #16
        bgt .Lloads_1b_inner

        subs x12, x12, #1
        bne .Lloads_1b_outer

        ret


.text
.align 6
loads_4b:
        mov x12, #10

.Lloads_4b_outer:
        mov  x13, x0  // Loop count from env var BENCHMARK_ITERATIONS
        adrp x9, data_buffer
        add  x9, x9, #:lo12:data_buffer

.Lloads_4b_inner:
        ldr w1, [x9]
        ldr w1, [x9, #4]
        ldr w1, [x9, #8]
        ldr w1, [x9, #12]
        ldr w1, [x9, #16]
        ldr w1, [x9, #20]
        ldr w1, [x9, #24]
        ldr w1, [x9, #28]
        ldr w1, [x9, #32]
        ldr w1, [x9, #36]
        ldr w1, [x9, #40]
        ldr w1, [x9, #44]
        ldr w1, [x9, #48]
        ldr w1, [x9, #52]
        ldr w1, [x9, #56]
        ldr w1, [x9, #60]
        add x9, x9, #64

        subs x13,x13, #16
        bgt .Lloads_4b_inner

        subs x12, x12, #1
        bne .Lloads_4b_outer

        ret


.text
.align 6
loads_8b:
        mov x12, #10

.Lloads_8b_outer:
        mov  x13, x0  // Loop count from env var BENCHMARK_ITERATIONS
        adrp x9, data_buffer
        add  x9, x9, #:lo12:data_buffer

.Lloads_8b_inner:
        ldr x1, [x9]
        ldr x1, [x9, #8]
        ldr x1, [x9, #16]
        ldr x1, [x9, #24]
        ldr x1, [x9, #32]
        ldr x1, [x9, #40]
        ldr x1, [x9, #48]
        ldr x1, [x9, #56]
        ldr x1, [x9, #64]
        ldr x1, [x9, #72]
        ldr x1, [x9, #80]
        ldr x1, [x9, #88]
        ldr x1, [x9, #96]
        ldr x1, [x9, #104]
        ldr x1, [x9, #112]
        ldr x1, [x9, #120]
        add x9, x9, #128

        subs x13,x13, #16
        bgt .Lloads_8b_inner

        subs x12, x12, #1
        bne .Lloads_8b_outer

        ret


.text
.align 6
loads_16b:
        mov x12, #10

.Lloads_16b_outer:
        mov  x13, x0  // Loop count from env var BENCHMARK_ITERATIONS
        adrp x9, data_buffer
        add  x9, x9, #:lo12:data_buffer

.Lloads_16b_inner:
        ldr q0, [x9]
        ldr q0, [x9, #16]
        ldr q0, [x9, #32]
        ldr q0, [x9, #48]
        ldr q0, [x9, #64]
        ldr q0, [x9, #80]
        ldr q0, [x9, #96]
        ldr q0, [x9, #112]
        ldr q0, [x9, #128]
        ldr q0, [x9, #144]
        ldr q0, [x9, #160]
        ldr q0, [x9, #176]
        ldr q0, [x9, #192]
        ldr q0, [x9, #208]
        ldr q0, [x9, #224]
        ldr q0, [x9, #240]
        add x9, x9, #256

        subs x13,x13, #16
        bgt .Lloads_16b_inner

        subs x12, x12, #1
        bne .Lloads_16b_outer

        ret


.text
.align 6
loads_64b:
        mov x12, #10

.Lloads_64b_outer:
        mov  x13, x0  // Loop count from env var BENCHMARK_ITERATIONS
        adrp x9, data_buffer
        add  x9, x9, #:lo12:data_buffer

        // One load per cache line, offset by 2b to avoid conflicts
.Lloads_64b_inner:
        ldrb w1, [x9]
        ldrb w1, [x9, #66]
        ldrb w1, [x9, #132]
        ldrb w1, [x9, #198]
        ldrb w1, [x9, #264]
        ldrb w1, [x9, #330]
        ldrb w1, [x9, #396]
        ldrb w1, [x9, #462]
        ldrb w1, [x9, #528]
        ldrb w1, [x9, #594]
        ldrb w1, [x9, #660]
        ldrb w1, [x9, #726]
        ldrb w1, [x9, #792]
        ldrb w1, [x9, #858]
        ldrb w1, [x9, #924]
        ldrb w1, [x9, #990]
        add x9, x9, #1024

        subs x13,x13, #16
        bgt .Lloads_64b_inner

        subs x12, x12, #1
        bne .Lloads_64b_outer

        ret


.text
.align 6
stores_1b:
        mov x12, #10

.Lstores_1b_outer:
        mov  x13, x0  // Loop count from env var BENCHMARK_ITERATIONS
        adrp x9, data_buffer
        add  x9, x9, #:lo12:data_buffer

.Lstores_1b_inner:
        strb w1, [x9]
        strb w1, [x9, #1]
        strb w1, [x9, #2]
        strb w1, [x9, #3]
        strb w1, [x9, #4]
        strb w1, [x9, #5]
        strb w1, [x9, #6]
        strb w1, [x9, #7]
        strb w1, [x9, #8]
        strb w1, [x9, #9]
        strb w1, [x9, #10]
        strb w1, [x9, #11]
        strb w1, [x9, #12]
        strb w1, [x9, #13]
        strb w1, [x9, #14]
        strb w1, [x9, #15]
        add x9, x9, #16

        subs x13,x13, #16
        bgt .Lstores_1b_inner

        subs x12, x12, #1
        bne .Lstores_1b_outer

        ret


.text
.align 6
stores_4b:
        mov x12, #10

.Lstores_4b_outer:
        mov  x13, x0  // Loop count from env var BENCHMARK_ITERATIONS
        adrp x9, data_buffer
        add  x9, x9, #:lo12:data_buffer

.Lstores_4b_inner:
        str w1, [x9]
        str w1, [x9, #4]
        str w1, [x9, #8]
        str w1, [x9, #12]
        str w1, [x9, #16]
        str w1, [x9, #20]
        str w1, [x9, #24]
        str w1, [x9, #28]
        str w1, [x9, #32]
        str w1, [x9, #36]
        str w1, [x9, #40]
        str w1, [x9, #44]
        str w1, [x9, #48]
        str w1, [x9, #52]
        str w1, [x9, #56]
        str w1, [x9, #60]
        add x9, x9, #64

        subs x13,x13, #16
        bgt .Lstores_4b_inner

        subs x12, x12, #1
        bne .Lstores_4b_outer

        ret


.text
.align 6
stores_8b:
        mov x12, #10

.Lstores_8b_outer:
        mov  x13, x0  // Loop count from env var BENCHMARK_ITERATIONS
        adrp x9, data_buffer
        add  x9, x9, #:lo12:data_buffer

.Lstores_8b_inner:
        str x1, [x9]
        str x1, [x9, #8]
        str x1, [x9, #16]
        str x1, [x9, #24]
        str x1, [x9, #32]
        str x1, [x9, #40]
        str x1, [x9, #48]
        str x1, [x9, #56]
        str x1, [x9, #64]
        str x1, [x9, #72]
        str x1, [x9, #80]
        str x1, [x9, #88]
        str x1, [x9, #96]
        str x1, [x9, #104]
        str x1, [x9, #112]
        str x1, [x9, #120]
        add x9, x9, #128

        subs x13,x13, #16
        bgt .Lstores_8b_inner

        subs x12, x12, #1
        bne .Lstores_8b_outer

        ret


.text
.align 6
stores_16b:
        mov x12, #10

.Lstores_16b_outer:
        mov  x13, x0  // Loop count from env var BENCHMARK_ITERATIONS
        adrp x9, data_buffer
        add  x9, x9, #:lo12:data_buffer

.Lstores_16b_inner:
        str q0, [x9]
        str q0, [x9, #16]
        str q0, [x9, #32]
        str q0, [x9, #48]
        str q0, [x9, #64]
        str q0, [x9, #80]
        str q0, [x9, #96]
        str q0, [x9, #112]
        str q0, [x9, #128]
        str q0, [x9, #144]
        str q0, [x9, #160]
        str q0, [x9, #176]
        str q0, [x9, #192]
        str q0, [x9, #208]
        str q0, [x9, #224]
        str q0, [x9, #240]
        add x9, x9, #256

        subs x13,x13, #16
        bgt .Lstores_16b_inner

        subs x12, x12, #1
        bne .Lstores_16b_outer

        ret


.text
.align 6
stores_64b:
        mov x12, #10

.Lstores_64b_outer:
        mov  x13, x0  // Loop count from env var BENCHMARK_ITERATIONS
        adrp x9, data_buffer
        add  x9, x9, #:lo12:data_buffer

        // One store per cache line, offset by 2b
.Lstores_64b_inner:
        strb w1, [x9]
        strb w1, [x9, #66]
        strb w1, [x9, #132]
        strb w1, [x9, #198]
        strb w1, [x9, #264]
        strb w1, [x9, #330]
        strb w1, [x9, #396]
        strb w1, [x9, #462]
        strb w1, [x9, #528]
        strb w1, [x9, #594]
        strb w1, [x9, #660]
        strb w1, [x9, #726]
        strb w1, [x9, #792]
        strb w1, [x9, #858]
        strb w1, [x9, #924]
        strb w1, [x9, #990]
        add x9, x9, #1024

        subs x13,x13, #16
        bgt .Lstores_64b_inner

        subs x12, x12, #1
        bne .Lstores_64b_outer

        ret
