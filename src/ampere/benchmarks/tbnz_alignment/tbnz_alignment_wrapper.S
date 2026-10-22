//
// Copyright (c) 2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//


// Instantiate several versions of the tbnz_alignment body code
// with varying values for loop return alignment and branch size.
// In order to test a relevant subset of interations between the
// LSB in an instruction address and several slightly higher order
// bits that may collide with that bit in some hashing scheme we
// sweep alignment from 0 to 24 bytes in 4 byte increments
// (0 to 6 instructions offset) and loop size from 16 to 512 bytes
// in powers of two checking the instruction address LSB (bit 2)
// for interactions with bits 4-9

#ifdef TBNZ_ALIGNMENT
#undef TBNZ_ALIGNMENT
#endif

#define TBNZ_ALIGNMENT 0

#ifdef TBNZ_SIZE
#undef TBNZ_SIZE
#endif

#define TBNZ_SIZE 16

#ifdef LOOP_LABEL 
#undef LOOP_LABEL 
#endif

#define LOOP_LABEL .Ltbnz_loop_0_16

#ifdef SKIP_LABEL 
#undef SKIP_LABEL 
#endif

#define SKIP_LABEL .Ltbnz_skip_0_16

.globl tbnz_alignment_a00_s016

tbnz_alignment_a00_s016:
    mov x1, x0
    b   LOOP_LABEL

#include "tbnz_alignment_body.S"

    mov x0, #0
    ret

#ifdef TBNZ_ALIGNMENT
#undef TBNZ_ALIGNMENT
#endif

#define TBNZ_ALIGNMENT 0

#ifdef TBNZ_SIZE
#undef TBNZ_SIZE
#endif

#define TBNZ_SIZE 32

#ifdef LOOP_LABEL 
#undef LOOP_LABEL 
#endif

#define LOOP_LABEL .Ltbnz_loop_0_32

#ifdef SKIP_LABEL 
#undef SKIP_LABEL 
#endif

#define SKIP_LABEL .Ltbnz_skip_0_32

.globl tbnz_alignment_a00_s032

tbnz_alignment_a00_s032:
    mov x1, x0
    b   LOOP_LABEL

#include "tbnz_alignment_body.S"

    mov x0, #0
    ret

#ifdef TBNZ_ALIGNMENT
#undef TBNZ_ALIGNMENT
#endif

#define TBNZ_ALIGNMENT 0

#ifdef TBNZ_SIZE
#undef TBNZ_SIZE
#endif

#define TBNZ_SIZE 64

#ifdef LOOP_LABEL 
#undef LOOP_LABEL 
#endif

#define LOOP_LABEL .Ltbnz_loop_0_64

#ifdef SKIP_LABEL 
#undef SKIP_LABEL 
#endif

#define SKIP_LABEL .Ltbnz_skip_0_64

.globl tbnz_alignment_a00_s064

tbnz_alignment_a00_s064:
    mov x1, x0
    b   LOOP_LABEL

#include "tbnz_alignment_body.S"

    mov x0, #0
    ret

#ifdef TBNZ_ALIGNMENT
#undef TBNZ_ALIGNMENT
#endif

#define TBNZ_ALIGNMENT 0

#ifdef TBNZ_SIZE
#undef TBNZ_SIZE
#endif

#define TBNZ_SIZE 128

#ifdef LOOP_LABEL 
#undef LOOP_LABEL 
#endif

#define LOOP_LABEL .Ltbnz_loop_0_128

#ifdef SKIP_LABEL 
#undef SKIP_LABEL 
#endif

#define SKIP_LABEL .Ltbnz_skip_0_128

.globl tbnz_alignment_a00_s128

tbnz_alignment_a00_s128:
    mov x1, x0
    b   LOOP_LABEL

#include "tbnz_alignment_body.S"

    mov x0, #0
    ret

#ifdef TBNZ_ALIGNMENT
#undef TBNZ_ALIGNMENT
#endif

#define TBNZ_ALIGNMENT 0

#ifdef TBNZ_SIZE
#undef TBNZ_SIZE
#endif

#define TBNZ_SIZE 256

#ifdef LOOP_LABEL 
#undef LOOP_LABEL 
#endif

#define LOOP_LABEL .Ltbnz_loop_0_256

#ifdef SKIP_LABEL 
#undef SKIP_LABEL 
#endif

#define SKIP_LABEL .Ltbnz_skip_0_256

.globl tbnz_alignment_a00_s256

tbnz_alignment_a00_s256:
    mov x1, x0
    b   LOOP_LABEL

#include "tbnz_alignment_body.S"

    mov x0, #0
    ret

#ifdef TBNZ_ALIGNMENT
#undef TBNZ_ALIGNMENT
#endif

#define TBNZ_ALIGNMENT 0

#ifdef TBNZ_SIZE
#undef TBNZ_SIZE
#endif

#define TBNZ_SIZE 512

#ifdef LOOP_LABEL 
#undef LOOP_LABEL 
#endif

#define LOOP_LABEL .Ltbnz_loop_0_512

#ifdef SKIP_LABEL 
#undef SKIP_LABEL 
#endif

#define SKIP_LABEL .Ltbnz_skip_0_512

.globl tbnz_alignment_a00_s512

tbnz_alignment_a00_s512:
    mov x1, x0
    b   LOOP_LABEL

#include "tbnz_alignment_body.S"

    mov x0, #0
    ret

#ifdef TBNZ_ALIGNMENT
#undef TBNZ_ALIGNMENT
#endif

#define TBNZ_ALIGNMENT 4

#ifdef TBNZ_SIZE
#undef TBNZ_SIZE
#endif

#define TBNZ_SIZE 16

#ifdef LOOP_LABEL 
#undef LOOP_LABEL 
#endif

#define LOOP_LABEL .Ltbnz_loop_4_16

#ifdef SKIP_LABEL 
#undef SKIP_LABEL 
#endif

#define SKIP_LABEL .Ltbnz_skip_4_16

.globl tbnz_alignment_a04_s016

tbnz_alignment_a04_s016:
    mov x1, x0
    b   LOOP_LABEL

#include "tbnz_alignment_body.S"

    mov x0, #0
    ret

#ifdef TBNZ_ALIGNMENT
#undef TBNZ_ALIGNMENT
#endif

#define TBNZ_ALIGNMENT 4

#ifdef TBNZ_SIZE
#undef TBNZ_SIZE
#endif

#define TBNZ_SIZE 32

#ifdef LOOP_LABEL 
#undef LOOP_LABEL 
#endif

#define LOOP_LABEL .Ltbnz_loop_4_32

#ifdef SKIP_LABEL 
#undef SKIP_LABEL 
#endif

#define SKIP_LABEL .Ltbnz_skip_4_32

.globl tbnz_alignment_a04_s032

tbnz_alignment_a04_s032:
    mov x1, x0
    b   LOOP_LABEL

#include "tbnz_alignment_body.S"

    mov x0, #0
    ret

#ifdef TBNZ_ALIGNMENT
#undef TBNZ_ALIGNMENT
#endif

#define TBNZ_ALIGNMENT 4

#ifdef TBNZ_SIZE
#undef TBNZ_SIZE
#endif

#define TBNZ_SIZE 64

#ifdef LOOP_LABEL 
#undef LOOP_LABEL 
#endif

#define LOOP_LABEL .Ltbnz_loop_4_64

#ifdef SKIP_LABEL 
#undef SKIP_LABEL 
#endif

#define SKIP_LABEL .Ltbnz_skip_4_64

.globl tbnz_alignment_a04_s064

tbnz_alignment_a04_s064:
    mov x1, x0
    b   LOOP_LABEL

#include "tbnz_alignment_body.S"

    mov x0, #0
    ret

#ifdef TBNZ_ALIGNMENT
#undef TBNZ_ALIGNMENT
#endif

#define TBNZ_ALIGNMENT 4

#ifdef TBNZ_SIZE
#undef TBNZ_SIZE
#endif

#define TBNZ_SIZE 128

#ifdef LOOP_LABEL 
#undef LOOP_LABEL 
#endif

#define LOOP_LABEL .Ltbnz_loop_4_128

#ifdef SKIP_LABEL 
#undef SKIP_LABEL 
#endif

#define SKIP_LABEL .Ltbnz_skip_4_128

.globl tbnz_alignment_a04_s128

tbnz_alignment_a04_s128:
    mov x1, x0
    b   LOOP_LABEL

#include "tbnz_alignment_body.S"

    mov x0, #0
    ret

#ifdef TBNZ_ALIGNMENT
#undef TBNZ_ALIGNMENT
#endif

#define TBNZ_ALIGNMENT 4

#ifdef TBNZ_SIZE
#undef TBNZ_SIZE
#endif

#define TBNZ_SIZE 256

#ifdef LOOP_LABEL 
#undef LOOP_LABEL 
#endif

#define LOOP_LABEL .Ltbnz_loop_4_256

#ifdef SKIP_LABEL 
#undef SKIP_LABEL 
#endif

#define SKIP_LABEL .Ltbnz_skip_4_256

.globl tbnz_alignment_a04_s256

tbnz_alignment_a04_s256:
    mov x1, x0
    b   LOOP_LABEL

#include "tbnz_alignment_body.S"

    mov x0, #0
    ret

#ifdef TBNZ_ALIGNMENT
#undef TBNZ_ALIGNMENT
#endif

#define TBNZ_ALIGNMENT 4

#ifdef TBNZ_SIZE
#undef TBNZ_SIZE
#endif

#define TBNZ_SIZE 512

#ifdef LOOP_LABEL 
#undef LOOP_LABEL 
#endif

#define LOOP_LABEL .Ltbnz_loop_4_512

#ifdef SKIP_LABEL 
#undef SKIP_LABEL 
#endif

#define SKIP_LABEL .Ltbnz_skip_4_512

.globl tbnz_alignment_a04_s512

tbnz_alignment_a04_s512:
    mov x1, x0
    b   LOOP_LABEL

#include "tbnz_alignment_body.S"

    mov x0, #0
    ret

#ifdef TBNZ_ALIGNMENT
#undef TBNZ_ALIGNMENT
#endif

#define TBNZ_ALIGNMENT 8

#ifdef TBNZ_SIZE
#undef TBNZ_SIZE
#endif

#define TBNZ_SIZE 16

#ifdef LOOP_LABEL 
#undef LOOP_LABEL 
#endif

#define LOOP_LABEL .Ltbnz_loop_8_16

#ifdef SKIP_LABEL 
#undef SKIP_LABEL 
#endif

#define SKIP_LABEL .Ltbnz_skip_8_16

.globl tbnz_alignment_a08_s016

tbnz_alignment_a08_s016:
    mov x1, x0
    b   LOOP_LABEL

#include "tbnz_alignment_body.S"

    mov x0, #0
    ret

#ifdef TBNZ_ALIGNMENT
#undef TBNZ_ALIGNMENT
#endif

#define TBNZ_ALIGNMENT 8

#ifdef TBNZ_SIZE
#undef TBNZ_SIZE
#endif

#define TBNZ_SIZE 32

#ifdef LOOP_LABEL 
#undef LOOP_LABEL 
#endif

#define LOOP_LABEL .Ltbnz_loop_8_32

#ifdef SKIP_LABEL 
#undef SKIP_LABEL 
#endif

#define SKIP_LABEL .Ltbnz_skip_8_32

.globl tbnz_alignment_a08_s032

tbnz_alignment_a08_s032:
    mov x1, x0
    b   LOOP_LABEL

#include "tbnz_alignment_body.S"

    mov x0, #0
    ret

#ifdef TBNZ_ALIGNMENT
#undef TBNZ_ALIGNMENT
#endif

#define TBNZ_ALIGNMENT 8

#ifdef TBNZ_SIZE
#undef TBNZ_SIZE
#endif

#define TBNZ_SIZE 64

#ifdef LOOP_LABEL 
#undef LOOP_LABEL 
#endif

#define LOOP_LABEL .Ltbnz_loop_8_64

#ifdef SKIP_LABEL 
#undef SKIP_LABEL 
#endif

#define SKIP_LABEL .Ltbnz_skip_8_64

.globl tbnz_alignment_a08_s064

tbnz_alignment_a08_s064:
    mov x1, x0
    b   LOOP_LABEL

#include "tbnz_alignment_body.S"

    mov x0, #0
    ret

#ifdef TBNZ_ALIGNMENT
#undef TBNZ_ALIGNMENT
#endif

#define TBNZ_ALIGNMENT 8

#ifdef TBNZ_SIZE
#undef TBNZ_SIZE
#endif

#define TBNZ_SIZE 128

#ifdef LOOP_LABEL 
#undef LOOP_LABEL 
#endif

#define LOOP_LABEL .Ltbnz_loop_8_128

#ifdef SKIP_LABEL 
#undef SKIP_LABEL 
#endif

#define SKIP_LABEL .Ltbnz_skip_8_128

.globl tbnz_alignment_a08_s128

tbnz_alignment_a08_s128:
    mov x1, x0
    b   LOOP_LABEL

#include "tbnz_alignment_body.S"

    mov x0, #0
    ret

#ifdef TBNZ_ALIGNMENT
#undef TBNZ_ALIGNMENT
#endif

#define TBNZ_ALIGNMENT 8

#ifdef TBNZ_SIZE
#undef TBNZ_SIZE
#endif

#define TBNZ_SIZE 256

#ifdef LOOP_LABEL 
#undef LOOP_LABEL 
#endif

#define LOOP_LABEL .Ltbnz_loop_8_256

#ifdef SKIP_LABEL 
#undef SKIP_LABEL 
#endif

#define SKIP_LABEL .Ltbnz_skip_8_256

.globl tbnz_alignment_a08_s256

tbnz_alignment_a08_s256:
    mov x1, x0
    b   LOOP_LABEL

#include "tbnz_alignment_body.S"

    mov x0, #0
    ret

#ifdef TBNZ_ALIGNMENT
#undef TBNZ_ALIGNMENT
#endif

#define TBNZ_ALIGNMENT 8

#ifdef TBNZ_SIZE
#undef TBNZ_SIZE
#endif

#define TBNZ_SIZE 512

#ifdef LOOP_LABEL 
#undef LOOP_LABEL 
#endif

#define LOOP_LABEL .Ltbnz_loop_8_512

#ifdef SKIP_LABEL 
#undef SKIP_LABEL 
#endif

#define SKIP_LABEL .Ltbnz_skip_8_512

.globl tbnz_alignment_a08_s512

tbnz_alignment_a08_s512:
    mov x1, x0
    b   LOOP_LABEL

#include "tbnz_alignment_body.S"

    mov x0, #0
    ret

#ifdef TBNZ_ALIGNMENT
#undef TBNZ_ALIGNMENT
#endif

#define TBNZ_ALIGNMENT 12

#ifdef TBNZ_SIZE
#undef TBNZ_SIZE
#endif

#define TBNZ_SIZE 16

#ifdef LOOP_LABEL 
#undef LOOP_LABEL 
#endif

#define LOOP_LABEL .Ltbnz_loop_12_16

#ifdef SKIP_LABEL 
#undef SKIP_LABEL 
#endif

#define SKIP_LABEL .Ltbnz_skip_12_16

.globl tbnz_alignment_a12_s016

tbnz_alignment_a12_s016:
    mov x1, x0
    b   LOOP_LABEL

#include "tbnz_alignment_body.S"

    mov x0, #0
    ret

#ifdef TBNZ_ALIGNMENT
#undef TBNZ_ALIGNMENT
#endif

#define TBNZ_ALIGNMENT 12

#ifdef TBNZ_SIZE
#undef TBNZ_SIZE
#endif

#define TBNZ_SIZE 32

#ifdef LOOP_LABEL 
#undef LOOP_LABEL 
#endif

#define LOOP_LABEL .Ltbnz_loop_12_32

#ifdef SKIP_LABEL 
#undef SKIP_LABEL 
#endif

#define SKIP_LABEL .Ltbnz_skip_12_32

.globl tbnz_alignment_a12_s032

tbnz_alignment_a12_s032:
    mov x1, x0
    b   LOOP_LABEL

#include "tbnz_alignment_body.S"

    mov x0, #0
    ret

#ifdef TBNZ_ALIGNMENT
#undef TBNZ_ALIGNMENT
#endif

#define TBNZ_ALIGNMENT 12

#ifdef TBNZ_SIZE
#undef TBNZ_SIZE
#endif

#define TBNZ_SIZE 64

#ifdef LOOP_LABEL 
#undef LOOP_LABEL 
#endif

#define LOOP_LABEL .Ltbnz_loop_12_64

#ifdef SKIP_LABEL 
#undef SKIP_LABEL 
#endif

#define SKIP_LABEL .Ltbnz_skip_12_64

.globl tbnz_alignment_a12_s064

tbnz_alignment_a12_s064:
    mov x1, x0
    b   LOOP_LABEL

#include "tbnz_alignment_body.S"

    mov x0, #0
    ret

#ifdef TBNZ_ALIGNMENT
#undef TBNZ_ALIGNMENT
#endif

#define TBNZ_ALIGNMENT 12

#ifdef TBNZ_SIZE
#undef TBNZ_SIZE
#endif

#define TBNZ_SIZE 128

#ifdef LOOP_LABEL 
#undef LOOP_LABEL 
#endif

#define LOOP_LABEL .Ltbnz_loop_12_128

#ifdef SKIP_LABEL 
#undef SKIP_LABEL 
#endif

#define SKIP_LABEL .Ltbnz_skip_12_128

.globl tbnz_alignment_a12_s128

tbnz_alignment_a12_s128:
    mov x1, x0
    b   LOOP_LABEL

#include "tbnz_alignment_body.S"

    mov x0, #0
    ret

#ifdef TBNZ_ALIGNMENT
#undef TBNZ_ALIGNMENT
#endif

#define TBNZ_ALIGNMENT 12

#ifdef TBNZ_SIZE
#undef TBNZ_SIZE
#endif

#define TBNZ_SIZE 256

#ifdef LOOP_LABEL 
#undef LOOP_LABEL 
#endif

#define LOOP_LABEL .Ltbnz_loop_12_256

#ifdef SKIP_LABEL 
#undef SKIP_LABEL 
#endif

#define SKIP_LABEL .Ltbnz_skip_12_256

.globl tbnz_alignment_a12_s256

tbnz_alignment_a12_s256:
    mov x1, x0
    b   LOOP_LABEL

#include "tbnz_alignment_body.S"

    mov x0, #0
    ret

#ifdef TBNZ_ALIGNMENT
#undef TBNZ_ALIGNMENT
#endif

#define TBNZ_ALIGNMENT 12

#ifdef TBNZ_SIZE
#undef TBNZ_SIZE
#endif

#define TBNZ_SIZE 512

#ifdef LOOP_LABEL 
#undef LOOP_LABEL 
#endif

#define LOOP_LABEL .Ltbnz_loop_12_512

#ifdef SKIP_LABEL 
#undef SKIP_LABEL 
#endif

#define SKIP_LABEL .Ltbnz_skip_12_512

.globl tbnz_alignment_a12_s512

tbnz_alignment_a12_s512:
    mov x1, x0
    b   LOOP_LABEL

#include "tbnz_alignment_body.S"

    mov x0, #0
    ret

#ifdef TBNZ_ALIGNMENT
#undef TBNZ_ALIGNMENT
#endif

#define TBNZ_ALIGNMENT 16

#ifdef TBNZ_SIZE
#undef TBNZ_SIZE
#endif

#define TBNZ_SIZE 16

#ifdef LOOP_LABEL 
#undef LOOP_LABEL 
#endif

#define LOOP_LABEL .Ltbnz_loop_16_16

#ifdef SKIP_LABEL 
#undef SKIP_LABEL 
#endif

#define SKIP_LABEL .Ltbnz_skip_16_16

.globl tbnz_alignment_a16_s016

tbnz_alignment_a16_s016:
    mov x1, x0
    b   LOOP_LABEL

#include "tbnz_alignment_body.S"

    mov x0, #0
    ret

#ifdef TBNZ_ALIGNMENT
#undef TBNZ_ALIGNMENT
#endif

#define TBNZ_ALIGNMENT 16

#ifdef TBNZ_SIZE
#undef TBNZ_SIZE
#endif

#define TBNZ_SIZE 32

#ifdef LOOP_LABEL 
#undef LOOP_LABEL 
#endif

#define LOOP_LABEL .Ltbnz_loop_16_32

#ifdef SKIP_LABEL 
#undef SKIP_LABEL 
#endif

#define SKIP_LABEL .Ltbnz_skip_16_32

.globl tbnz_alignment_a16_s032

tbnz_alignment_a16_s032:
    mov x1, x0
    b   LOOP_LABEL

#include "tbnz_alignment_body.S"

    mov x0, #0
    ret

#ifdef TBNZ_ALIGNMENT
#undef TBNZ_ALIGNMENT
#endif

#define TBNZ_ALIGNMENT 16

#ifdef TBNZ_SIZE
#undef TBNZ_SIZE
#endif

#define TBNZ_SIZE 64

#ifdef LOOP_LABEL 
#undef LOOP_LABEL 
#endif

#define LOOP_LABEL .Ltbnz_loop_16_64

#ifdef SKIP_LABEL 
#undef SKIP_LABEL 
#endif

#define SKIP_LABEL .Ltbnz_skip_16_64

.globl tbnz_alignment_a16_s064

tbnz_alignment_a16_s064:
    mov x1, x0
    b   LOOP_LABEL

#include "tbnz_alignment_body.S"

    mov x0, #0
    ret

#ifdef TBNZ_ALIGNMENT
#undef TBNZ_ALIGNMENT
#endif

#define TBNZ_ALIGNMENT 16

#ifdef TBNZ_SIZE
#undef TBNZ_SIZE
#endif

#define TBNZ_SIZE 128

#ifdef LOOP_LABEL 
#undef LOOP_LABEL 
#endif

#define LOOP_LABEL .Ltbnz_loop_16_128

#ifdef SKIP_LABEL 
#undef SKIP_LABEL 
#endif

#define SKIP_LABEL .Ltbnz_skip_16_128

.globl tbnz_alignment_a16_s128

tbnz_alignment_a16_s128:
    mov x1, x0
    b   LOOP_LABEL

#include "tbnz_alignment_body.S"

    mov x0, #0
    ret

#ifdef TBNZ_ALIGNMENT
#undef TBNZ_ALIGNMENT
#endif

#define TBNZ_ALIGNMENT 16

#ifdef TBNZ_SIZE
#undef TBNZ_SIZE
#endif

#define TBNZ_SIZE 256

#ifdef LOOP_LABEL 
#undef LOOP_LABEL 
#endif

#define LOOP_LABEL .Ltbnz_loop_16_256

#ifdef SKIP_LABEL 
#undef SKIP_LABEL 
#endif

#define SKIP_LABEL .Ltbnz_skip_16_256

.globl tbnz_alignment_a16_s256

tbnz_alignment_a16_s256:
    mov x1, x0
    b   LOOP_LABEL

#include "tbnz_alignment_body.S"

    mov x0, #0
    ret

#ifdef TBNZ_ALIGNMENT
#undef TBNZ_ALIGNMENT
#endif

#define TBNZ_ALIGNMENT 16

#ifdef TBNZ_SIZE
#undef TBNZ_SIZE
#endif

#define TBNZ_SIZE 512

#ifdef LOOP_LABEL 
#undef LOOP_LABEL 
#endif

#define LOOP_LABEL .Ltbnz_loop_16_512

#ifdef SKIP_LABEL 
#undef SKIP_LABEL 
#endif

#define SKIP_LABEL .Ltbnz_skip_16_512

.globl tbnz_alignment_a16_s512

tbnz_alignment_a16_s512:
    mov x1, x0
    b   LOOP_LABEL

#include "tbnz_alignment_body.S"

    mov x0, #0
    ret

#ifdef TBNZ_ALIGNMENT
#undef TBNZ_ALIGNMENT
#endif

#define TBNZ_ALIGNMENT 20

#ifdef TBNZ_SIZE
#undef TBNZ_SIZE
#endif

#define TBNZ_SIZE 16

#ifdef LOOP_LABEL 
#undef LOOP_LABEL 
#endif

#define LOOP_LABEL .Ltbnz_loop_20_16

#ifdef SKIP_LABEL 
#undef SKIP_LABEL 
#endif

#define SKIP_LABEL .Ltbnz_skip_20_16

.globl tbnz_alignment_a20_s016

tbnz_alignment_a20_s016:
    mov x1, x0
    b   LOOP_LABEL

#include "tbnz_alignment_body.S"

    mov x0, #0
    ret

#ifdef TBNZ_ALIGNMENT
#undef TBNZ_ALIGNMENT
#endif

#define TBNZ_ALIGNMENT 20

#ifdef TBNZ_SIZE
#undef TBNZ_SIZE
#endif

#define TBNZ_SIZE 32

#ifdef LOOP_LABEL 
#undef LOOP_LABEL 
#endif

#define LOOP_LABEL .Ltbnz_loop_20_32

#ifdef SKIP_LABEL 
#undef SKIP_LABEL 
#endif

#define SKIP_LABEL .Ltbnz_skip_20_32

.globl tbnz_alignment_a20_s032

tbnz_alignment_a20_s032:
    mov x1, x0
    b   LOOP_LABEL

#include "tbnz_alignment_body.S"

    mov x0, #0
    ret

#ifdef TBNZ_ALIGNMENT
#undef TBNZ_ALIGNMENT
#endif

#define TBNZ_ALIGNMENT 20

#ifdef TBNZ_SIZE
#undef TBNZ_SIZE
#endif

#define TBNZ_SIZE 64

#ifdef LOOP_LABEL 
#undef LOOP_LABEL 
#endif

#define LOOP_LABEL .Ltbnz_loop_20_64

#ifdef SKIP_LABEL 
#undef SKIP_LABEL 
#endif

#define SKIP_LABEL .Ltbnz_skip_20_64

.globl tbnz_alignment_a20_s064

tbnz_alignment_a20_s064:
    mov x1, x0
    b   LOOP_LABEL

#include "tbnz_alignment_body.S"

    mov x0, #0
    ret

#ifdef TBNZ_ALIGNMENT
#undef TBNZ_ALIGNMENT
#endif

#define TBNZ_ALIGNMENT 20

#ifdef TBNZ_SIZE
#undef TBNZ_SIZE
#endif

#define TBNZ_SIZE 128

#ifdef LOOP_LABEL 
#undef LOOP_LABEL 
#endif

#define LOOP_LABEL .Ltbnz_loop_20_128

#ifdef SKIP_LABEL 
#undef SKIP_LABEL 
#endif

#define SKIP_LABEL .Ltbnz_skip_20_128

.globl tbnz_alignment_a20_s128

tbnz_alignment_a20_s128:
    mov x1, x0
    b   LOOP_LABEL

#include "tbnz_alignment_body.S"

    mov x0, #0
    ret

#ifdef TBNZ_ALIGNMENT
#undef TBNZ_ALIGNMENT
#endif

#define TBNZ_ALIGNMENT 20

#ifdef TBNZ_SIZE
#undef TBNZ_SIZE
#endif

#define TBNZ_SIZE 256

#ifdef LOOP_LABEL 
#undef LOOP_LABEL 
#endif

#define LOOP_LABEL .Ltbnz_loop_20_256

#ifdef SKIP_LABEL 
#undef SKIP_LABEL 
#endif

#define SKIP_LABEL .Ltbnz_skip_20_256

.globl tbnz_alignment_a20_s256

tbnz_alignment_a20_s256:
    mov x1, x0
    b   LOOP_LABEL

#include "tbnz_alignment_body.S"

    mov x0, #0
    ret

#ifdef TBNZ_ALIGNMENT
#undef TBNZ_ALIGNMENT
#endif

#define TBNZ_ALIGNMENT 20

#ifdef TBNZ_SIZE
#undef TBNZ_SIZE
#endif

#define TBNZ_SIZE 512

#ifdef LOOP_LABEL 
#undef LOOP_LABEL 
#endif

#define LOOP_LABEL .Ltbnz_loop_20_512

#ifdef SKIP_LABEL 
#undef SKIP_LABEL 
#endif

#define SKIP_LABEL .Ltbnz_skip_20_512

.globl tbnz_alignment_a20_s512

tbnz_alignment_a20_s512:
    mov x1, x0
    b   LOOP_LABEL

#include "tbnz_alignment_body.S"

    mov x0, #0
    ret

#ifdef TBNZ_ALIGNMENT
#undef TBNZ_ALIGNMENT
#endif

#define TBNZ_ALIGNMENT 24

#ifdef TBNZ_SIZE
#undef TBNZ_SIZE
#endif

#define TBNZ_SIZE 16

#ifdef LOOP_LABEL 
#undef LOOP_LABEL 
#endif

#define LOOP_LABEL .Ltbnz_loop_24_16

#ifdef SKIP_LABEL 
#undef SKIP_LABEL 
#endif

#define SKIP_LABEL .Ltbnz_skip_24_16

.globl tbnz_alignment_a24_s016

tbnz_alignment_a24_s016:
    mov x1, x0
    b   LOOP_LABEL

#include "tbnz_alignment_body.S"

    mov x0, #0
    ret

#ifdef TBNZ_ALIGNMENT
#undef TBNZ_ALIGNMENT
#endif

#define TBNZ_ALIGNMENT 24

#ifdef TBNZ_SIZE
#undef TBNZ_SIZE
#endif

#define TBNZ_SIZE 32

#ifdef LOOP_LABEL 
#undef LOOP_LABEL 
#endif

#define LOOP_LABEL .Ltbnz_loop_24_32

#ifdef SKIP_LABEL 
#undef SKIP_LABEL 
#endif

#define SKIP_LABEL .Ltbnz_skip_24_32

.globl tbnz_alignment_a24_s032

tbnz_alignment_a24_s032:
    mov x1, x0
    b   LOOP_LABEL

#include "tbnz_alignment_body.S"

    mov x0, #0
    ret

#ifdef TBNZ_ALIGNMENT
#undef TBNZ_ALIGNMENT
#endif

#define TBNZ_ALIGNMENT 24

#ifdef TBNZ_SIZE
#undef TBNZ_SIZE
#endif

#define TBNZ_SIZE 64

#ifdef LOOP_LABEL 
#undef LOOP_LABEL 
#endif

#define LOOP_LABEL .Ltbnz_loop_24_64

#ifdef SKIP_LABEL 
#undef SKIP_LABEL 
#endif

#define SKIP_LABEL .Ltbnz_skip_24_64

.globl tbnz_alignment_a24_s064

tbnz_alignment_a24_s064:
    mov x1, x0
    b   LOOP_LABEL

#include "tbnz_alignment_body.S"

    mov x0, #0
    ret

#ifdef TBNZ_ALIGNMENT
#undef TBNZ_ALIGNMENT
#endif

#define TBNZ_ALIGNMENT 24

#ifdef TBNZ_SIZE
#undef TBNZ_SIZE
#endif

#define TBNZ_SIZE 128

#ifdef LOOP_LABEL 
#undef LOOP_LABEL 
#endif

#define LOOP_LABEL .Ltbnz_loop_24_128

#ifdef SKIP_LABEL 
#undef SKIP_LABEL 
#endif

#define SKIP_LABEL .Ltbnz_skip_24_128

.globl tbnz_alignment_a24_s128

tbnz_alignment_a24_s128:
    mov x1, x0
    b   LOOP_LABEL

#include "tbnz_alignment_body.S"

    mov x0, #0
    ret

#ifdef TBNZ_ALIGNMENT
#undef TBNZ_ALIGNMENT
#endif

#define TBNZ_ALIGNMENT 24

#ifdef TBNZ_SIZE
#undef TBNZ_SIZE
#endif

#define TBNZ_SIZE 256

#ifdef LOOP_LABEL 
#undef LOOP_LABEL 
#endif

#define LOOP_LABEL .Ltbnz_loop_24_256

#ifdef SKIP_LABEL 
#undef SKIP_LABEL 
#endif

#define SKIP_LABEL .Ltbnz_skip_24_256

.globl tbnz_alignment_a24_s256

tbnz_alignment_a24_s256:
    mov x1, x0
    b   LOOP_LABEL

#include "tbnz_alignment_body.S"

    mov x0, #0
    ret

#ifdef TBNZ_ALIGNMENT
#undef TBNZ_ALIGNMENT
#endif

#define TBNZ_ALIGNMENT 24

#ifdef TBNZ_SIZE
#undef TBNZ_SIZE
#endif

#define TBNZ_SIZE 512

#ifdef LOOP_LABEL 
#undef LOOP_LABEL 
#endif

#define LOOP_LABEL .Ltbnz_loop_24_512

#ifdef SKIP_LABEL 
#undef SKIP_LABEL 
#endif

#define SKIP_LABEL .Ltbnz_skip_24_512

.globl tbnz_alignment_a24_s512

tbnz_alignment_a24_s512:
    mov x1, x0
    b   LOOP_LABEL

#include "tbnz_alignment_body.S"

    mov x0, #0
    ret


