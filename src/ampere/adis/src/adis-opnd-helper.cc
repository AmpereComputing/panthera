//
// Copyright (c) 2024 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include <cassert>
#include "adis-opnd-helper.h"

namespace adis {
static unsigned shift(unsigned esize, unsigned tsize, unsigned imm3) {
    return (2 * esize) - (tsize<<3|imm3);
}
static unsigned shift2(unsigned esize, unsigned tsize, unsigned imm3) {
    return (tsize<<3|imm3) - esize;
}

static unsigned tsize(unsigned tszh, unsigned tszl) {
    return (tszh << 2 ) | tszl;
}

static unsigned highest_bit_set(unsigned x) {
    unsigned pos = 99;
    unsigned y = x;
    unsigned i = 0;
    while (y) {
        if (y & 1) {
            pos = i;
        }
        y = y >> 1;
        i = i + 1;
    }
    assert(pos != 99);
    return pos;
}

static unsigned esize(unsigned tsize) {
    unsigned x = highest_bit_set(tsize);
    return 8 << x;
}

// create a human-understandable shift count from the encoding fields
unsigned shift_opnd(unsigned tszh, unsigned tszl, unsigned imm3) {
    assert(tszh !=0 || tszl != 0); // cannot both be zero
    unsigned tsize_v = tsize(tszh, tszl);
    unsigned esize_v = esize(tsize_v);
    unsigned shift_v = shift(esize_v, tsize_v, imm3);
    return shift_v;
}

unsigned shift2_opnd(unsigned tszh, unsigned tszl, unsigned imm3) {
    assert(tszh !=0 || tszl != 0); // cannot both be zero
    unsigned tsize_v = tsize(tszh, tszl);
    unsigned esize_v = esize(tsize_v);
    unsigned shift_v = shift2(esize_v, tsize_v, imm3);
    return shift_v;
}

} // namespace
