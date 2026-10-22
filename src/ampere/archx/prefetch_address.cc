//
// Copyright (c) 2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <inttypes.h>

#include "ampere/sc/sc.h"
#include "ampere/archx/prefetch_address.h"
#include "ampere/archx/decoder_input.h"
#include "ampere/adis/src/adis.h"

namespace ampere::archx {

static int64_t sign_extend(int64_t x, int64_t bit)
{
    int64_t m = 1ULL << (bit - 1);
    return (x ^ m) - m;
}

VirtAddr prefetch_immediate_address(uint32_t opcode, uint64_t rn)
{
    assert(adis::decode(opcode) == adis::ENC_PRFM_P_ldst_pos);
    uint64_t pimm = util::extract(opcode, 21, 10) << 3;
    assert(pimm <= 32760);
    return VirtAddr(rn + pimm);
}

VirtAddr prefetch_literal_address(ProgramCounter pc, uint32_t opcode)
{
    assert(adis::decode(opcode) == adis::ENC_PRFM_P_loadlit);
    int64_t offset = sign_extend(util::extract(opcode, 23, 5), 19) << 2;
    assert(offset < 1 << 20 && offset >= -1 * (1 << 20));
    return VirtAddr(pc + offset);
}

VirtAddr prefetch_register_address(uint32_t opcode, uint64_t rn, uint64_t rm)
{
    assert(adis::decode(opcode) == adis::ENC_PRFM_P_ldst_regoff);
    bool nonsigned = util::extract(opcode, 15, 15) != 1;
    unsigned len = util::extract(opcode, 13, 13) == 1 ? 64 : 32;
    unsigned shift = util::extract(opcode, 12, 12) == 1 ? 3 : 0;
    uint64_t offset;

    // Default if 'option' field is left blank is for LSL
    if (util::extract(opcode, 15, 13) == 0) {
        len = 64;
    }

    if (64 - shift < len)
        len = 64 - shift;

    // zero- or sign-extend based on encoding
    if (nonsigned) {
        if (len != 64)
            offset = rm & ((1UL << len) - 1);
        else
            offset = rm;
    } else {
        offset = sign_extend(rm, len);
    }
    offset <<= shift;

    return VirtAddr(rn + offset);
}

VirtAddr prefetch_unscaled_address(uint32_t opcode, uint64_t rn)
{
    assert(adis::decode(opcode) == adis::ENC_PRFUM_P_ldst_unscaled);
    int64_t simm = sign_extend(util::extract(opcode, 20, 12), 9);
    assert(simm >= -256 && simm <= 255);
    return VirtAddr(rn + simm);
}

} // namespace ampere::archx
