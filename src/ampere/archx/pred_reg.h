//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once
#include <cstdint>

namespace ampere::archx {
// VL=128b=16B, predicates are 1b per byte. If we ever grow VL, need to modify this.
typedef std::uint16_t pred_reg_t;

union preg_t
{
    pred_reg_t p;
    std::uint8_t u8[sizeof(pred_reg_t)];
};

static inline bool get_pred_bit8(pred_reg_t preg, std::uint32_t element)
{
    return (preg >> (element)) & 1;
}
static inline bool get_pred_bit16(pred_reg_t preg, std::uint32_t element)
{
    return (preg >> (element * 2)) & 1;
}
static inline bool get_pred_bit32(pred_reg_t preg, std::uint32_t element)
{
    return (preg >> (element * 4)) & 1;
}
static inline bool get_pred_bit64(pred_reg_t preg, std::uint32_t element)
{
    return (preg >> (element * 8)) & 1;
}

[[maybe_unused]] static inline bool get_pred_bit(pred_reg_t preg, std::uint32_t element, std::uint32_t esize)
{
    switch (esize) {
    case 8:
        return get_pred_bit8(preg, element);
    case 16:
        return get_pred_bit16(preg, element);
    case 32:
        return get_pred_bit32(preg, element);
    case 64:
        return get_pred_bit64(preg, element);
    default:
        return false;
    }
}

static inline bool pred_all_zero(pred_reg_t preg, std::uint32_t esize)
{ // FIXME: 16b governing predicate assumed
    switch (esize) {
    case 8:
        return (preg & 0xFFFF) == 0;
    case 16:
        return (preg & 0x5555) == 0;
    case 32:
        return (preg & 0x1111) == 0;
    case 64:
        return (preg & 0x0101) == 0;
    default:
        return false;
    }
}

static inline bool pred_all_active(pred_reg_t preg, std::uint32_t esize)
{ // FIXME: 16b governing predicate assumed
    switch (esize) {
    case 8:
        return (preg & 0xFFFF) == 0xFFFF;
    case 16:
        return (preg & 0x5555) == 0x5555;
    case 32:
        return (preg & 0x1111) == 0x1111;
    case 64:
        return (preg & 0x0101) == 0x0101;
    default:
        return false;
    }
}

} // namespace ampere::archx
