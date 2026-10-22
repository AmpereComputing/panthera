//
// Copyright (c) 2018-2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#pragma once

#include <cstdint>
#include <string>
#include "ampere/util/enum.h"

make_ns_enum_without_class(ampere::archx, LReg,
    L0,
    L1,
    L2,
    L3,
    L4,
    L5,
    L6,
    L7,
    L8,
    L9,
    L10,
    L11,
    L12,
    L13,
    L14,
    L15,
    L16,
    L17,
    L18,
    L19,
    L20,
    L21,
    L22,
    L23,
    L24,
    L25,
    L26,
    L27,
    L28,
    L29,
    L30,
    ZR,
    SP,
    s0 = 36,
    s1,
    V0,
    V1,
    V2,
    V3,
    V4,
    V5,
    V6,
    V7,
    V8,
    V9,
    V10,
    V11,
    V12,
    V13,
    V14,
    V15,
    V16,
    V17,
    V18,
    V19,
    V20,
    V21,
    V22,
    V23,
    V24,
    V25,
    V26,
    V27,
    V28,
    V29,
    V30,
    V31,
    t0,
    t1,
    t2,
    t3,
    t4,
    t5,
    t6,
    t7,
    t8,
    xt0,
    xt1,
    FLAGS,
    FLGT0, // temp flag register
    P0,  // SVE predicate registers
    P1,
    P2,
    P3,
    P4,
    P5,
    P6,
    P7,
    P8,
    P9,
    P10,
    P11,
    P12,
    P13,
    P14,
    P15,
    FFR, // SVE first fault register
    SP_EL1_REG,
    SP_EL2_REG,
    SP_EL3_REG,
    RGSR_REG,
    INVALID
);

namespace ampere::archx {

std::string regstr(LReg r);
std::string nzcv_str(std::uint32_t nzcv);

inline LReg cvt_gpr(std::uint32_t v, LReg all_ones = LReg::INVALID)
{
    if (v == 0x1f && all_ones != LReg::INVALID)
        return all_ones;
    else
        return LReg(v);
}

inline LReg cvt_vec(std::uint32_t v)
{
    return LReg(v + V0);
}

inline LReg cvt_pred(std::uint32_t v)
{
    if (v == FFR)
        return LReg::FFR;
    return LReg(v + LReg::P0);
}

inline LReg cvt_tmpflag(std::uint32_t v)
{ //  this gets v==38 ... the arbitrary signal value defined in
    //  sve/lib/python3/proj/xlat_register_constants.py
    return LReg::FLGT0;
    (void)v; // ignored
}

inline LReg cvt_flag(std::uint32_t v)
{
    if (v == 0)
        return LReg::FLAGS;
    else
        return LReg::FLGT0;
}

inline bool invalid_reg(LReg r)
{
    bool retval{false};
    switch (r) {
    case LReg::ZR:
    case LReg::INVALID:
        retval = true;
        break;
    default:
        retval = false;
        break;
    }
    return retval;
}

inline bool is_integer(LReg r)
{
    if (r >= LReg::L0 && r <= LReg::L30)
        return true;
    if (r == LReg::SP)
        return true;
    if (r >= LReg::s0 && r <= LReg::s1)
        return true;
    return false;
}

inline bool is_vector(LReg r)
{
    if (r >= LReg::V0 && r <= LReg::V31)
        return true;
    if (r >= LReg::t0 && r <= LReg::t8)
        return true;
    return false;
}

inline bool is_predicate_strict(LReg r)
{
    return r >= LReg::P0 && r <= LReg::P15;
}

inline bool is_predicate(LReg r)
{
    if (is_predicate_strict(r))
        return true;
    if (r == LReg::FFR)
        return true;
    return false;
}

inline bool is_flag(LReg r)
{
    return r >= LReg::FLAGS && r <= LReg::FLGT0;
}

} // namespace ampere::archx

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
