//
// Copyright (c) 2018-2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#ifndef AMPERE_TRACING_ENCODINGS_H
#define AMPERE_TRACING_ENCODINGS_H

#include <inttypes.h>

#include "ampere/archx/cache_maint_type.h"
#include "ampere/archx/divide_type.h"
#include "ampere/archx/sysreg.h"
#include "ampere/archx/decoder_input.h"
#include "ampere/adis/src/adis.h"

using namespace ampere::archx;

extern int decode_cache_maint(const unsigned char* bytes);
extern int decode_divide(const unsigned char* bytes);

/* Return true if the instruction encoding is an AArch64 branch instruction. */
inline bool is_branch(uint32_t encoding)
{
    encoding &= 0xfe000000;
    return encoding == 0x54000000 ||              // conditional branch (imm)
           encoding == 0xd6000000 ||              // unconditional branch (reg)
           (encoding & 0x5c000000) == 0x14000000; // unconditional branch (imm) or [CT]BN?Z
}

inline adis::encoding_enum_t adecode(uint32_t opcode)
{
    return adis::decode(opcode);
}

inline adis::sys_alias_t cache_maint_type(adis::encoding_enum_t adis_info)
{
    if (adis::ic(adis_info) || adis::dc(adis_info))
        return adis::sysql(adis_info);
    return adis::sys_alias_t::INVALID;
}

inline DivideType divide_type(uint32_t opcode)
{
    DecoderInput di(opcode);
    return DivideType(decode_divide(di.ptr_no_mode()));
}
#define DIV_RN(opcode) (((0x1f << 5) & opcode) >> 5)
#define DIV_RM(opcode) (((0x1f << 16) & opcode) >> 16)
#define SVE_LOW_REG(opcode) ((opcode) & 0x1f)
#define SVE_2ND_REG(opcode) (((opcode) >> 5) & 0x1f)

#define PREF_RT(opcode) (opcode & 0x1f)
#define PREF_RN(opcode) (((0x1f << 5) & opcode) >> 5)
#define PREF_RM(opcode) (((0x1f << 16) & opcode) >> 16)

#define MSR_OP0(opcode) ((((0x1 << 19) & opcode) >> 19) + 0x2)
#define MSR_OP1(opcode) (((0x7 << 16) & opcode) >> 16)
#define MSR_CRN(opcode) (((0xf << 12) & opcode) >> 12)
#define MSR_CRM(opcode) (((0xf << 8) & opcode) >> 8)
#define MSR_OP2(opcode) (((0x7 << 5) & opcode) >> 5)
#define MSR_REG(opcode) (opcode & 0x1f)
#define MRS_REG(opcode) MSR_REG(opcode)

#define IS_ERETAA_AB(opcode) ((opcode & 0xfffffbff) == 0xd69f0bff)
#define IS_ERET(opcode) (opcode == 0xd69f03e0 || IS_ERETAA_AB(opcode))
#define IS_SVC(opcode) ((opcode & 0xffe0001f) == 0xd4000001)
#define IS_HVC(opcode) ((opcode & 0xffe0001f) == 0xd4000002)
#define IS_SMC(opcode) ((opcode & 0xffe0001f) == 0xd4000003)
#define IS_HINT(opcode, imm) (opcode == ((imm << 5) | 0xd503201f))
#define IS_HLT(opcode, imm) (opcode == ((imm << 5) | 0xd4400000))

#endif // AMPERE_TRACING_ENCODINGS_H

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
