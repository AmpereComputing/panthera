//
// Copyright (c) 2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#ifndef TBNZ_ALIGNMENT
#define TBNZ_ALIGNMENT 0
#endif

#ifndef TBNZ_LOOP_SIZE
#define TBNZ_LOOP_SIZE 64
#endif

#if TBNZ_ALIGNMENT & 0x3 != 0
#error "TBNZ_ALIGNMENT must be divisible by 4 bytes"
#endif

#if TBNZ_LOOP_SIZE & 0x3 != 0
#error "TBNZ_LOOP_SIZE must be divisible by 4 bytes"
#endif

#define TBNZ_ALIGNMENT_PADDING (TBNZ_ALIGNMENT % 64)/4
#define TBNZ_LOOP_PADDING (TBNZ_LOOP_SIZE-12)/4

.balign 4096
    // Offset the loop return a specific number of bytes
    // away from a highly aligned (ie, many low-bites 0)
    // location to allow seeing the interaction between
    // low offset bits in the bp hashes by injecting nop
    // instructions
.rept TBNZ_ALIGNMENT_PADDING
    nop
.endr
LOOP_LABEL:

    tbnz    x1, 0, SKIP_LABEL

    // Make the loop size a specific number of bytes to
    // allow for testing the interaction of a lower order
    // bit from the varying offset and a slightly higher
    // order bit differentiating the two branch locations
    // when the size is a power of 2
.rept TBNZ_LOOP_PADDING
    add     x2, x1, #1
.endr

SKIP_LABEL:
    subs    x1, x1, 1
    bne     LOOP_LABEL
