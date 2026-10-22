//
// Copyright (c) 2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once
#include "adis-encoding-enum.h"
namespace adis {
static inline bool eret(encoding_enum_t r) {
    return r == ENC_ERET_64E_branch_reg;
}
static inline bool eret_aa(encoding_enum_t r) {
    return r == ENC_ERETAA_64E_branch_reg;
}
static inline bool eret_ab(encoding_enum_t r) {
    return r == ENC_ERETAB_64E_branch_reg;
}
static inline bool svc(encoding_enum_t r) {
    return r == ENC_SVC_EX_exception;
}
static inline bool hvc(encoding_enum_t r) {
    return r == ENC_HVC_EX_exception;
}
static inline bool smc(encoding_enum_t r) {
    return r == ENC_SMC_EX_exception;
}
static inline bool hlt(encoding_enum_t r) {
    return r == ENC_HLT_EX_exception;
}
static inline bool hint(encoding_enum_t r) {
    return r == ENC_HINT_HM_hints;
}
static inline bool nop(encoding_enum_t r) {
    return r == ENC_NOP_HI_hints;
}
static inline bool aese(encoding_enum_t r) {
    return r == ENC_AESE_B_cryptoaes || r == ENC_aese_z_zz_;
}
static inline bool aesd(encoding_enum_t r) {
    return r == ENC_AESD_B_cryptoaes || r == ENC_aesd_z_zz_;
}
static inline bool dc_zva(encoding_enum_t r) {
    return r == ENC_SYS_CR_systeminstrs_DC_ZVA;
}
static inline bool dc_gzva(encoding_enum_t r) {
    return r == ENC_SYS_CR_systeminstrs_DC_GZVA;
}
static inline bool clrex(encoding_enum_t r) {
    return r == ENC_CLREX_BN_barriers;
}
static inline bool msr_imm(encoding_enum_t r) { // move imm to pstate
    return r == ENC_MSR_SI_pstate;
}
static inline bool msr_reg(encoding_enum_t r) { // move gpr to msr
    return r == ENC_MSR_SR_systemmove;
}
static inline bool msr(encoding_enum_t r) { // write msr, either of the above
    return msr_reg(r) || msr_imm(r);
}
static inline bool mrs(encoding_enum_t r) { // read sysreg to gpr
    return r == ENC_MRS_RS_systemmove;
}

} // namespace
