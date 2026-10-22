//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <stdexcept>
#include <sstream>
//#include <iostream>
#include "adis-strtab-enum.h"
#include "adis-sysreg.h"
#include "adis-dec-imm13.h"

namespace adis {


static inline std::uint32_t adis_nreg(uint32_t x) {
    return x%0x3F; // mod 32
}

typedef enum     {
    ADIS_VDATA_INVALID,

    ADIS_VDATA_WREG,
    ADIS_VDATA_XREG,

    ADIS_VDATA_VREG, // unsized... refined to one of the following B/H/W/D/Q
    ADIS_VDATA_VRB,  //8b elem
    ADIS_VDATA_VRH,  //16b elem
    ADIS_VDATA_VRS,  //32b elem
    ADIS_VDATA_VRD,  //64b elem
    ADIS_VDATA_VRQ,  //128b elem

    ADIS_VDATA_ZREG, // unsized... refined to one of the following B/H/W/D/Q
    ADIS_VDATA_ZRB,  // 8b elem
    ADIS_VDATA_ZRH,  //16b elem
    ADIS_VDATA_ZRW,  //32b elem
    ADIS_VDATA_ZRD,  //64b elem
    ADIS_VDATA_ZRQ,  //128b elem

    ADIS_VDATA_ZA, // tile
    ADIS_VDATA_ZA_indirect, // tile  specified indirectly (SME: LDR ZA[Wv,imm])

    ADIS_VDATA_PREDREG,  // SVE predicate
    ADIS_VDATA_PREDREG_GOVERNING,  // SVE predicate guard
    ADIS_VDATA_FFR,      // SVE first fault register
    ADIS_VDATA_FLAGS,
    ADIS_VDATA_FPCR,
    ADIS_VDATA_FPSR,

    ADIS_VDATA_BT,
    ADIS_VDATA_COND,
    ADIS_VDATA_STRTAB,

    //...
    ADIS_VDATA_WSP, // SP_EL0
    ADIS_VDATA_SP, // SP_EL0
    ADIS_VDATA_SP_EL0,
    ADIS_VDATA_SP_EL1,
    ADIS_VDATA_SP_EL2,
    ADIS_VDATA_SP_EL3,

    ADIS_VDATA_SPSR_EL1,
    ADIS_VDATA_SPSR_EL2,
    ADIS_VDATA_SPSR_EL3,

    ADIS_VDATA_ELR_EL1,
    ADIS_VDATA_ELR_EL2,
    ADIS_VDATA_ELR_EL3,

    ADIS_VDATA_PC,
    ADIS_VDATA_WZR,
    ADIS_VDATA_XZR,
    

    ADIS_VDATA_IMM,  // unsized...
    ADIS_VDATA_IMM_PCPLUS,
    ADIS_VDATA_IMM_PCMINUS,  
    ADIS_VDATA_SIMM, // stored in u32 so need care extracting/using
    ADIS_VDATA_SIMM_PCPLUS,
    ADIS_VDATA_SIMM_PCPAGE,
    ADIS_VDATA_SIMM_PCMINUS,
    ADIS_VDATA_IMM64,  // uses out-of-array storage in uimm64 field
    ADIS_VDATA_IMMR,
    ADIS_VDATA_IMMS,
    ADIS_VDATA_AMOUNT,
    ADIS_VDATA_EXTEND,
    ADIS_VDATA_SYSTEMREG,
    ADIS_VDATA_MRRS,
    ADIS_VDATA_MRS,
    ADIS_VDATA_MSR_reg,
    ADIS_VDATA_MSR_imm,
    ADIS_VDATA_MSRR,
    ADIS_VDATA_LAST,

} adis_vdata_enum_t;

typedef struct {
    adis_vdata_enum_t etype;
    union {
        std::uint16_t id;
        std::uint32_t u32;
        std::int32_t  i32;
        adis_strtab_enum_t str_enum;
    } u;
} adis_vdata_element_t;

static inline void adis_vdata_element_set_id(adis_vdata_element_t* d, std::uint32_t v) {
    d->u.id = v;
}
static inline std::uint16_t adis_vdata_element_get_id(const adis_vdata_element_t* d) {
    return d->u.id;
}
static inline std::uint16_t adis_vdata_element_get_id(const adis_vdata_element_t& d) {
    return d.u.id;
}
static inline void adis_vdata_element_set_u32(adis_vdata_element_t* d, std::uint32_t v) {
    d->u.u32 = v;
}
static inline void adis_vdata_element_set_i32(adis_vdata_element_t* d, std::int32_t v) {
    d->u.i32 = v;
}
static inline void adis_vdata_element_set_str_enum(adis_vdata_element_t* d, adis_strtab_enum_t v) {
    d->u.str_enum = v;
}
static inline adis_strtab_enum_t adis_vdata_element_get_str_enum(const adis_vdata_element_t* d) {
    return d->u.str_enum;
}
static inline adis_strtab_enum_t adis_vdata_element_get_str_enum(const adis_vdata_element_t& d) {
    return d.u.str_enum;
}

static inline std::uint32_t adis_vdata_element_get_u32(const adis_vdata_element_t* d)  {
    return d->u.u32;
}
static inline std::uint32_t adis_vdata_element_get_u32(const adis_vdata_element_t& d)  {
    return d.u.u32;
}
static inline std::int32_t  adis_vdata_element_get_i32(const adis_vdata_element_t* d) {
    return d->u.i32;
}
static inline std::int32_t  adis_vdata_element_get_i32(const adis_vdata_element_t& d) {
    return d.u.i32;
}

static inline void adis_vdata_element_set_enum(adis_vdata_element_t* d, adis_vdata_enum_t n) {
    d->etype = n;
}
static inline adis_vdata_enum_t adis_vdata_element_get_enum(const adis_vdata_element_t* d)  {
    return d->etype;
}
static inline adis_vdata_enum_t adis_vdata_element_get_enum(const adis_vdata_element_t& d)  {
    return d.etype;
}
static inline void adis_vdata_element_set(adis_vdata_element_t* d, adis_vdata_enum_t n, std::uint32_t v) {
    adis_vdata_element_set_enum(d,n);
    adis_vdata_element_set_id(d,v);
}






typedef enum {
    ADIS_ERROR_NONE,
    ADIS_ERROR_BAD_N_IMMS_IMMR,
    ADIS_ERROR_BAD_OPERAND_TABLE_ENTRY
} adis_error_enum_t;

#define ADIS_MAX_VDATA_ELEMENTS 10
struct adis_vdata_t {

    adis_vdata_element_t elements[ADIS_MAX_VDATA_ELEMENTS];
    std::uint32_t nelement;
    std::uint64_t uimm64;
    adis_error_enum_t error;

    adis_vdata_t() : nelement(0), uimm64(0), error(ADIS_ERROR_NONE) {}

    const adis_vdata_element_t& get(std::uint_fast32_t i) const {
        if (i > nelement)
            throw std::runtime_error("too many elements sought from adis_vdata_t");
        return elements[i];
    }
};

static inline adis_vdata_t* adis_vdata_alloc() {
    adis_vdata_t* v = new adis_vdata_t;
    return v;
}

typedef struct {
    std::uint32_t itext;
    std::uint32_t inum;
} adis_data_t;

static inline void adis_vdata_set_error(adis_vdata_t* d, adis_error_enum_t e) {
    d->error = e;
}
static inline adis_error_enum_t adis_vdata_get_error(adis_vdata_t* d) {
    return d->error;
}
static inline adis_error_enum_t adis_vdata_get_error(const adis_vdata_t& d) {
    return d.error;
}

static inline void adis_vdata_set_id(adis_vdata_t* d, std::uint32_t pos, std::uint32_t v) {
    assert(pos < ADIS_MAX_VDATA_ELEMENTS);
    adis_vdata_element_set_id(d->elements+pos, v);
}
static inline void adis_vdata_set_u32(adis_vdata_t* d, std::uint32_t pos, std::uint32_t v) {
    assert(pos < ADIS_MAX_VDATA_ELEMENTS);
    adis_vdata_element_set_u32(d->elements+pos, v);
}
static inline void adis_vdata_set_i32(adis_vdata_t* d, std::uint32_t pos, std::int32_t v) {
    assert(pos < ADIS_MAX_VDATA_ELEMENTS);
    adis_vdata_element_set_i32(d->elements+pos, v);
}

static inline void adis_vdata_set_enum(adis_vdata_t* d, std::uint32_t pos, adis_vdata_enum_t n) {
    assert(pos < ADIS_MAX_VDATA_ELEMENTS);
    adis_vdata_element_set_enum(d->elements+pos, n);
}
static inline adis_vdata_enum_t adis_vdata_get_enum(const adis_vdata_t* d, std::uint32_t pos) {
    assert(pos < ADIS_MAX_VDATA_ELEMENTS);
    return adis_vdata_element_get_enum(d->elements+pos);
}

static inline void adis_vdata_set(adis_vdata_t* d, std::uint32_t pos, adis_vdata_enum_t n, std::uint32_t v) {
    adis_vdata_set_enum(d, pos, n);
    adis_vdata_set_id(d, pos, v);
}
static inline void adis_vdata_set_u32(adis_vdata_t* d, std::uint32_t pos, adis_vdata_enum_t n, std::uint32_t v) {
    adis_vdata_set_enum(d, pos, n);
    adis_vdata_set_u32(d, pos, v);
}
static inline void adis_vdata_set_i32(adis_vdata_t* d, std::uint32_t pos, adis_vdata_enum_t n, std::int32_t v) {
    adis_vdata_set_enum(d, pos, n);
    adis_vdata_set_i32(d, pos, v);
}
static inline void adis_vdata_set_imm64(adis_vdata_t* d, std::uint32_t pos, adis_vdata_enum_t n, std::uint64_t v) {
    adis_vdata_set_enum(d, pos, n);
    adis_vdata_set_u32(d, pos, 0);
    d->uimm64 = v; // stored seperately to save space
}

/// strtab
static inline void adis_vdata_element_set_strtab(adis_vdata_element_t* d,
                                                 adis_strtab_enum_t v) {
    adis_vdata_element_set_enum(d, ADIS_VDATA_STRTAB);
    adis_vdata_element_set_str_enum(d,v);
}

static inline void adis_vdata_set_strtab(adis_vdata_t* d, std::uint32_t pos, adis_strtab_enum_t v) {
    assert(pos < ADIS_MAX_VDATA_ELEMENTS);
    adis_vdata_element_set_strtab(d->elements+pos, v);
}
static inline adis_strtab_enum_t adis_vdata_get_str_enum(adis_vdata_t* d, std::uint32_t pos) {
    assert(pos < ADIS_MAX_VDATA_ELEMENTS);
    return adis_vdata_element_get_str_enum(d->elements+pos);
}
static inline adis_strtab_enum_t adis_vdata_get_str_enum(const adis_vdata_t& d, std::uint32_t pos) {
    assert(pos < ADIS_MAX_VDATA_ELEMENTS);
    return adis_vdata_element_get_str_enum(d.elements[pos]);
}

static inline void adis_vdat_strtab(adis_vdata_t* d,
                                    adis_strtab_enum_t str_enum) {
    std::uint32_t pos = d->nelement++;
    adis_vdata_set_strtab(d, pos, str_enum);
}




static inline void adis_vdat_Wn_or_WSP(adis_vdata_t* d, std::uint32_t rn) {
    std::uint32_t pos = d->nelement++;
    if (rn == 31) {
        adis_vdata_set(d, pos, ADIS_VDATA_WSP, 0);
    }
    else {
        adis_vdata_set(d, pos, ADIS_VDATA_WREG, rn);
    }
}
static inline void adis_vdat_Wn_or_WZR(adis_vdata_t* d, std::uint32_t rn) {
    std::uint32_t pos = d->nelement++;
    if (rn == 31) {
        adis_vdata_set(d, pos, ADIS_VDATA_WZR, 0);
    }
    else {
        adis_vdata_set(d, pos, ADIS_VDATA_WREG, rn);
    }
}

static inline void adis_vdat_Wd_or_WSP(adis_vdata_t* d, std::uint32_t rn) {
    adis_vdat_Wn_or_WSP(d, rn);
}
static inline void adis_vdat_Xn_or_SP(adis_vdata_t* d, std::uint32_t rn) {
    std::uint32_t pos = d->nelement++;
    if (rn == 31) {
        adis_vdata_set(d, pos, ADIS_VDATA_SP, 0);
    }
    else {
        adis_vdata_set(d, pos, ADIS_VDATA_XREG, rn);
    }
}
static inline void adis_vdat_Xn_or_XZR(adis_vdata_t* d, std::uint32_t rn) {
    std::uint32_t pos = d->nelement++;
    if (rn == 31) {
        adis_vdata_set(d, pos, ADIS_VDATA_XZR, 0);
    }
    else {
        adis_vdata_set(d, pos, ADIS_VDATA_XREG, rn);
    }
}

static inline void adis_vdat_Wa(adis_vdata_t* d, std::uint32_t x) {
    adis_vdat_Wn_or_WZR(d,x);
}
static inline void adis_vdat_Wdn(adis_vdata_t* d, std::uint32_t x) {
    adis_vdat_Wn_or_WZR(d,x);
}
static inline void adis_vdat_Wt1(adis_vdata_t* d, std::uint32_t x) {
    adis_vdat_Wn_or_WZR(d,x);
}
static inline void adis_vdat_Wt2(adis_vdata_t* d, std::uint32_t x) {
    adis_vdat_Wn_or_WZR(d,x);
}
static inline void adis_vdat_Wv(adis_vdata_t* d, std::uint32_t x) {
    adis_vdat_Wn_or_WZR(d,x);
}

static inline void adis_vdat_Xa(adis_vdata_t* d, std::uint32_t x) {
    adis_vdat_Xn_or_XZR(d,x);
}
static inline void adis_vdat_Xdn(adis_vdata_t* d, std::uint32_t x) {
    adis_vdat_Xn_or_XZR(d,x);
}
static inline void adis_vdat_Xt1(adis_vdata_t* d, std::uint32_t x) {
    adis_vdat_Xn_or_XZR(d,x);
}
static inline void adis_vdat_Xt2(adis_vdata_t* d, std::uint32_t x) {
    adis_vdat_Xn_or_XZR(d,x);
}


static inline void adis_vdat_Xt_or_SP(adis_vdata_t* d, std::uint32_t rn) {
    adis_vdat_Xn_or_SP(d, rn);
}
static inline void adis_vdat_Xd_or_SP(adis_vdata_t* d, std::uint32_t rn) {
    adis_vdat_Xn_or_SP(d, rn);
}
static inline void adis_vdat_Xm_or_SP(adis_vdata_t* d, std::uint32_t rn) {
    adis_vdat_Xn_or_SP(d, rn);
}

static inline void adis_vdat_R(adis_vdata_t* d, std::uint32_t option) {
    // ADD/SUB (extended register) encode a W register except for UXTX and
    // SXTX, whose option values end in binary 11 and select an X register.
    std::uint32_t pos = d->nelement++;
    adis_vdata_set(d, pos, (option & 0x3) == 0x3 ? ADIS_VDATA_XREG : ADIS_VDATA_WREG, 0);
}

static inline void adis_vdat_R_from_imm5(adis_vdata_t* d, std::uint32_t imm5)
{
    // AdvSIMD scalar-register forms select X only for the D-element
    // encoding, whose low four imm5 bits are 1000.
    std::uint32_t pos = d->nelement++;
    adis_vdata_set(d, pos, (imm5 & 0xf) == 0x8 ? ADIS_VDATA_XREG : ADIS_VDATA_WREG, 0);
}

void adis_vdat_table_cleanup(adis_vdata_t* d, std::uint32_t pos);

static inline void adis_vdat_n_or_SP(adis_vdata_t* d, std::uint32_t n) {
    // this is a secondary function...
    std::uint32_t pos = d->nelement-1;
    adis_vdat_table_cleanup(d,pos);

    adis_vdata_enum_t etype = adis_vdata_get_enum(d,pos);
    if (etype == ADIS_VDATA_WREG)  {
        if (n == 31) {
            adis_vdata_set(d, pos, ADIS_VDATA_WSP, 0);
        }
        else {
            adis_vdata_set(d, pos, ADIS_VDATA_WREG, n);
        }
    }
    else if (etype == ADIS_VDATA_XREG)  {
        if (n == 31) {
            adis_vdata_set(d, pos, ADIS_VDATA_SP, 0);
        }
        else {
            adis_vdata_set(d, pos, ADIS_VDATA_XREG, n);
        }
    }
}


static inline void adis_vdat_n_or_ZR(adis_vdata_t* d, std::uint32_t n) {
    // this is a secondary function...
    std::uint32_t pos = d->nelement-1;
    adis_vdat_table_cleanup(d,pos);

    adis_vdata_enum_t etype = adis_vdata_get_enum(d,pos);
    switch(etype) {
    case ADIS_VDATA_WREG:
        if (n == 31) {
            adis_vdata_set(d, pos, ADIS_VDATA_WZR, 0);
        }
        else {
            adis_vdata_set(d, pos, ADIS_VDATA_WREG, n);
        }
        break;
    case ADIS_VDATA_XREG:
        if (n == 31) {
            adis_vdata_set(d, pos, ADIS_VDATA_XZR, 0);

            adis_vdata_set(d, pos, ADIS_VDATA_SP, 0);
        }
        else {
            adis_vdata_set(d, pos, ADIS_VDATA_XREG, n);
        }
        break;
    case ADIS_VDATA_VRB:
        adis_vdata_set(d, pos, ADIS_VDATA_VRB, n);
        break;
    case ADIS_VDATA_VRH:
        adis_vdata_set(d, pos, ADIS_VDATA_VRH, n);
        break;
    case ADIS_VDATA_VRS:
        adis_vdata_set(d, pos, ADIS_VDATA_VRS, n);
        break;
    case ADIS_VDATA_VRD:
        adis_vdata_set(d, pos, ADIS_VDATA_VRD, n);
        break;
    default:
        throw std::runtime_error("unexpected secondary register type");
    }
}



static inline void adis_vdat_Xd(adis_vdata_t* d, std::uint32_t xd) {
    adis_vdat_Xn_or_XZR(d,xd);
}
static inline void adis_vdat_Xm(adis_vdata_t* d, std::uint32_t xm) {
    adis_vdat_Xn_or_XZR(d,xm);
}
static inline void adis_vdat_Xs(adis_vdata_t* d, std::uint32_t xs) {
    adis_vdat_Xn_or_XZR(d,xs);
}
static inline void adis_vdat_Xn(adis_vdata_t* d, std::uint32_t xn) {
    adis_vdat_Xn_or_XZR(d,xn);
}
static inline void adis_vdat_Xt(adis_vdata_t* d, std::uint32_t xt) {
    adis_vdat_Xn_or_XZR(d,xt);
}

static inline void adis_vdat_Wd(adis_vdata_t* d, std::uint32_t w) {
    adis_vdat_Wn_or_WZR(d,w);
}
static inline void adis_vdat_Wt(adis_vdata_t* d, std::uint32_t w) {
    adis_vdat_Wn_or_WZR(d,w);
}
static inline void adis_vdat_Ws(adis_vdata_t* d, std::uint32_t w) {
    adis_vdat_Wn_or_WZR(d,w);
}
static inline void adis_vdat_Wn(adis_vdata_t* d, std::uint32_t w) {
    adis_vdat_Wn_or_WZR(d,w);
}
static inline void adis_vdat_Wm(adis_vdata_t* d, std::uint32_t w) {
    adis_vdat_Wn_or_WZR(d,w);
}


static inline void adis_vdat_Pg(adis_vdata_t* d, std::uint32_t n) {
    std::uint32_t pos = d->nelement++;
    adis_vdata_set(d, pos, ADIS_VDATA_PREDREG_GOVERNING, n);
}
static inline void adis_vdat_Pv(adis_vdata_t* d, std::uint32_t n) {
    std::uint32_t pos = d->nelement++;
    adis_vdata_set(d, pos, ADIS_VDATA_PREDREG, n);
}
static inline void adis_vdat_Pm(adis_vdata_t* d, std::uint32_t n) {
    adis_vdat_Pv(d,n);
}
static inline void adis_vdat_Pd(adis_vdata_t* d, std::uint32_t n) {
    adis_vdat_Pv(d,n);
}
static inline void adis_vdat_Pn(adis_vdata_t* d, std::uint32_t n) {
    adis_vdat_Pv(d,n);
}
static inline void adis_vdat_Pt(adis_vdata_t* d, std::uint32_t n) {
    adis_vdat_Pv(d,n);
}
static inline void adis_vdat_Pdm(adis_vdata_t* d, std::uint32_t n) {
    adis_vdat_Pv(d,n);
}
static inline void adis_vdat_Pdn(adis_vdata_t* d, std::uint32_t n) {
    adis_vdat_Pv(d,n);
}



static inline void adis_vdat_PNg(adis_vdata_t* d, std::uint32_t pr) {
    // Use P8...P15 with an implicit leading 1 in the encoding.
    std::uint32_t pos = d->nelement++;
    adis_vdata_set(d, pos, ADIS_VDATA_PREDREG_GOVERNING, pr+8);
}
static inline void adis_vdat_PNn(adis_vdata_t* d, std::uint32_t pr) {
    // Use P8...P15 with an implicit leading 1 in the encoding.

    // CNTP uses PNn for P0...P7. The generator handles that form
    // explicitly.
    std::uint32_t pos = d->nelement++;
    adis_vdata_set(d, pos, ADIS_VDATA_PREDREG, pr+8);
}
static inline void adis_vdat_PNd(adis_vdata_t* d, std::uint32_t pr) {
    // Use P8...P15 with an implicit leading 1 in the encoding.
    adis_vdat_PNn(d,pr);
}


static inline void adis_vdat_Z(adis_vdata_t* d) {
    std::uint32_t pos = d->nelement++;
    adis_vdata_set(d, pos, ADIS_VDATA_ZREG, 0);
}
static inline void adis_vdat_Zn(adis_vdata_t* d, std::uint32_t x) {
    std::uint32_t pos = d->nelement++;
    adis_vdata_set(d, pos, ADIS_VDATA_ZREG, x);
}
static inline void adis_vdat_Zm(adis_vdata_t* d, std::uint32_t x) {
    adis_vdat_Zn(d,x);
}
static inline void adis_vdat_Zdn(adis_vdata_t* d, std::uint32_t x) {
    adis_vdat_Zn(d,x);
}
static inline void adis_vdat_Zda(adis_vdata_t* d, std::uint32_t x) {
    adis_vdat_Zn(d,x);
}
static inline void adis_vdat_Zd(adis_vdata_t* d, std::uint32_t x) {
    adis_vdat_Zn(d,x);
}
static inline void adis_vdat_Zt(adis_vdata_t* d, std::uint32_t x) {
    adis_vdat_Zn(d,x);
}
static inline void adis_vdat_Za(adis_vdata_t* d, std::uint32_t x) {
    adis_vdat_Zn(d,x);
}
static inline void adis_vdat_Zk(adis_vdata_t* d, std::uint32_t x) {
    adis_vdat_Zn(d,x);
}
static inline void adis_vdat_Zn1(adis_vdata_t* d, std::uint32_t x) {
    adis_vdat_Zn(d,x);
}
static inline void adis_vdat_Zn2(adis_vdata_t* d, std::uint32_t x) {
    adis_vdat_Zn(d, adis_nreg(x+1));
}
static inline void adis_vdat_Zt1(adis_vdata_t* d, std::uint32_t x) {
    adis_vdat_Zn(d,x);
}
static inline void adis_vdat_Zt2(adis_vdata_t* d, std::uint32_t x) {
    adis_vdat_Zn(d, adis_nreg(x+1));
}
static inline void adis_vdat_Zt3(adis_vdata_t* d, std::uint32_t x) {
    adis_vdat_Zn(d, adis_nreg(x+2));
}
static inline void adis_vdat_Zt4(adis_vdata_t* d, std::uint32_t x) {
    adis_vdat_Zn(d, adis_nreg(x+3));
}
static inline void adis_vdat_Zda1(adis_vdata_t* d, std::uint32_t x) {
    adis_vdat_Zn(d,2*x);
}
static inline void adis_vdat_Zda2(adis_vdata_t* d, std::uint32_t x) {
    adis_vdat_Zn(d,2*x+1);
}

static inline void adis_vdat_Zdn1(adis_vdata_t* d, std::uint32_t x) {
    adis_vdat_Zn(d,2*x);
}
static inline void adis_vdat_Zdn2(adis_vdata_t* d, std::uint32_t x) {
    adis_vdat_Zn(d,2*x+1);
}
static inline void adis_vdat_Zdn4(adis_vdata_t* d, std::uint32_t x) {
    adis_vdat_Zn(d,4*x+3);
}
static inline void adis_vdat_Zd1(adis_vdata_t* d, std::uint32_t x) {
    adis_vdat_Zn(d,2*x);
}
static inline void adis_vdat_Zd2(adis_vdata_t* d, std::uint32_t x) {
    adis_vdat_Zn(d,2*x+1);
}


static inline void adis_vdat_Qd(adis_vdata_t* d, std::uint32_t n) {
    std::uint32_t pos = d->nelement++;
    adis_vdata_set(d, pos, ADIS_VDATA_VRQ, n);
}
static inline void adis_vdat_Qn(adis_vdata_t* d, std::uint32_t x) {
    adis_vdat_Qd(d,x);
}
static inline void adis_vdat_Qt(adis_vdata_t* d, std::uint32_t x) {
    adis_vdat_Qd(d,x);
}
static inline void adis_vdat_Qt1(adis_vdata_t* d, std::uint32_t x) {
    adis_vdat_Qd(d,x);
}
static inline void adis_vdat_Qt2(adis_vdata_t* d, std::uint32_t x) {
    adis_vdat_Qd(d,x);
}




static inline void adis_vdat_Dn(adis_vdata_t* d, std::uint32_t n) {
    std::uint32_t pos = d->nelement++;
    adis_vdata_set(d, pos, ADIS_VDATA_VRD, n);
}
static inline void adis_vdat_Dd(adis_vdata_t* d, std::uint32_t n) {
    std::uint32_t pos = d->nelement++;
    adis_vdata_set(d, pos, ADIS_VDATA_VRD, n);
}
static inline void adis_vdat_Dm(adis_vdata_t* d, std::uint32_t n) {
    std::uint32_t pos = d->nelement++;
    adis_vdata_set(d, pos, ADIS_VDATA_VRD, n);
}
static inline void adis_vdat_Dt(adis_vdata_t* d, std::uint32_t n) {
    std::uint32_t pos = d->nelement++;
    adis_vdata_set(d, pos, ADIS_VDATA_VRD, n);
}
static inline void adis_vdat_Dt1(adis_vdata_t* d, std::uint32_t n) {
    std::uint32_t pos = d->nelement++;
    adis_vdata_set(d, pos, ADIS_VDATA_VRD, n);
}
static inline void adis_vdat_Dt2(adis_vdata_t* d, std::uint32_t n) {
    std::uint32_t pos = d->nelement++;
    adis_vdata_set(d, pos, ADIS_VDATA_VRD, n);
}
static inline void adis_vdat_Da(adis_vdata_t* d, std::uint32_t n) {
    std::uint32_t pos = d->nelement++;
    adis_vdata_set(d, pos, ADIS_VDATA_VRD, n);
}
static inline void adis_vdat_Ds(adis_vdata_t* d, std::uint32_t n) {
    std::uint32_t pos = d->nelement++;
    adis_vdata_set(d, pos, ADIS_VDATA_VRD, n);
}

static inline void adis_vdat_Sn(adis_vdata_t* d, std::uint32_t n) {
    std::uint32_t pos = d->nelement++;
    adis_vdata_set(d, pos, ADIS_VDATA_VRS, n);
}
static inline void adis_vdat_Sd(adis_vdata_t* d, std::uint32_t n) {
    std::uint32_t pos = d->nelement++;
    adis_vdata_set(d, pos, ADIS_VDATA_VRS, n);
}
static inline void adis_vdat_Sm(adis_vdata_t* d, std::uint32_t n) {
    std::uint32_t pos = d->nelement++;
    adis_vdata_set(d, pos, ADIS_VDATA_VRS, n);
}
static inline void adis_vdat_St(adis_vdata_t* d, std::uint32_t n) {
    std::uint32_t pos = d->nelement++;
    adis_vdata_set(d, pos, ADIS_VDATA_VRS, n);
}
static inline void adis_vdat_Sa(adis_vdata_t* d, std::uint32_t n) {
    std::uint32_t pos = d->nelement++;
    adis_vdata_set(d, pos, ADIS_VDATA_VRS, n);
}
static inline void adis_vdat_Ss(adis_vdata_t* d, std::uint32_t n) {
    std::uint32_t pos = d->nelement++;
    adis_vdata_set(d, pos, ADIS_VDATA_VRS, n);
}
static inline void adis_vdat_St1(adis_vdata_t* d, std::uint32_t n) {
    std::uint32_t pos = d->nelement++;
    adis_vdata_set(d, pos, ADIS_VDATA_VRS, n);
}
static inline void adis_vdat_St2(adis_vdata_t* d, std::uint32_t n) {
    std::uint32_t pos = d->nelement++;
    adis_vdata_set(d, pos, ADIS_VDATA_VRS, n);
}




static inline void adis_vdat_Hn(adis_vdata_t* d, std::uint32_t rn) {
    std::uint32_t pos = d->nelement++;
    adis_vdata_set(d, pos, ADIS_VDATA_VRH, rn);
}
static inline void adis_vdat_Hd(adis_vdata_t* d, std::uint32_t rn) {
    std::uint32_t pos = d->nelement++;
    adis_vdata_set(d, pos, ADIS_VDATA_VRH, rn);
}
static inline void adis_vdat_Hm(adis_vdata_t* d, std::uint32_t rn) {
    std::uint32_t pos = d->nelement++;
    adis_vdata_set(d, pos, ADIS_VDATA_VRH, rn);
}
static inline void adis_vdat_Ht(adis_vdata_t* d, std::uint32_t rn) {
    std::uint32_t pos = d->nelement++;
    adis_vdata_set(d, pos, ADIS_VDATA_VRH, rn);
}
static inline void adis_vdat_Ha(adis_vdata_t* d, std::uint32_t rn) {
    std::uint32_t pos = d->nelement++;
    adis_vdata_set(d, pos, ADIS_VDATA_VRH, rn);
}
static inline void adis_vdat_Hs(adis_vdata_t* d, std::uint32_t rn) {
    std::uint32_t pos = d->nelement++;
    adis_vdata_set(d, pos, ADIS_VDATA_VRH, rn);
}



// could chose to not increment these as they'll be type-refined but
// it seems less error prone to reach backwards than to delay
// incrementing.
static inline void adis_vdat_V(adis_vdata_t* d) { //FIXME: this seems wrong...
    // For the FP16 FMAXNMV and friends. Just set "H" for next adis_vdat_d() call to finish
    std::uint32_t pos = d->nelement++;
    adis_vdata_set(d, pos, ADIS_VDATA_VRH, 0);
}

static inline void adis_vdat_Vreg(adis_vdata_t* d) { //FIXME: name
    std::uint32_t pos = d->nelement++;
    adis_vdata_set(d, pos, ADIS_VDATA_VREG, 0);
}

static inline void adis_vdat_Vn(adis_vdata_t* d, std::uint32_t rn) {
    std::uint32_t pos = d->nelement++;
    adis_vdata_set(d, pos, ADIS_VDATA_VREG, rn);
}
static inline void adis_vdat_Vm(adis_vdata_t* d, std::uint32_t rn) {
    adis_vdat_Vn(d,rn);
}
static inline void adis_vdat_Vd(adis_vdata_t* d, std::uint32_t rn) {
    adis_vdat_Vn(d,rn);
}
static inline void adis_vdat_Va(adis_vdata_t* d, std::uint32_t rn) {
    adis_vdat_Vn(d,rn);
}
static inline void adis_vdat_Vt(adis_vdata_t* d, std::uint32_t rn) {
    adis_vdat_Vn(d,rn);
}
static inline void adis_vdat_Vt2(adis_vdata_t* d, std::uint32_t rn) {
    adis_vdat_Vn(d,adis_nreg(rn+1));
}
static inline void adis_vdat_Vt3(adis_vdata_t* d, std::uint32_t rn) {
    adis_vdat_Vn(d,adis_nreg(rn+2));
}
static inline void adis_vdat_Vt4(adis_vdata_t* d, std::uint32_t rn) {
    adis_vdat_Vn(d,adis_nreg(rn+3));
}
static inline void adis_vdat_Vn1(adis_vdata_t* d, std::uint32_t n) {
    adis_vdat_Vn(d,n);
}
static inline void adis_vdat_Vn2(adis_vdata_t* d, std::uint32_t n) {
    adis_vdat_Vn(d,n);
}

static inline void adis_vdat_systemreg(adis_vdata_t* d, std::uint32_t imm) {
    std::uint32_t pos = d->nelement++;
    adis_vdata_set_u32(d, pos, ADIS_VDATA_SYSTEMREG, 0x8000|imm); // add implicit 1 to op0
}

static inline void adis_vdata_set_sysreg(adis_vdata_t* d, std::uint32_t pos,
                                         adis_vdata_enum_t n,
                                         std::uint32_t v)
{
    adis_vdata_set_u32(d, pos, n, v);
}

static inline void adis_vdat_MRRS(adis_vdata_t* d, std::uint32_t s) {
    std::uint32_t pos = d->nelement++;
    adis_vdata_set_sysreg(d, pos, ADIS_VDATA_MRRS, s);
}
static inline void adis_vdat_MRS(adis_vdata_t* d, std::uint32_t s) {
    std::uint32_t pos = d->nelement++;
    adis_vdata_set_sysreg(d, pos, ADIS_VDATA_MRS, s);
}
static inline void adis_vdat_MSR_reg(adis_vdata_t* d, std::uint32_t s) {
    std::uint32_t pos = d->nelement++;
    adis_vdata_set_sysreg(d, pos, ADIS_VDATA_MSR_reg, s);
}
static inline void adis_vdat_MSR_imm(adis_vdata_t* d, std::uint32_t s) {
    std::uint32_t pos = d->nelement++;
    adis_vdata_set_sysreg(d, pos, ADIS_VDATA_MSR_imm, s);
}
static inline void adis_vdat_MSRR(adis_vdata_t* d, std::uint32_t s) {
    std::uint32_t pos = d->nelement++;
    adis_vdata_set_sysreg(d, pos, ADIS_VDATA_MSRR, s);
}

// immediates

static inline void adis_vdat_amount(adis_vdata_t* d, std::uint32_t imm) {
    std::uint32_t pos = d->nelement++;
    adis_vdata_set(d, pos, ADIS_VDATA_AMOUNT, imm);
}

static inline void adis_vdat_immr(adis_vdata_t* d, std::uint32_t imm) {
    std::uint32_t pos = d->nelement++;
    adis_vdata_set(d, pos, ADIS_VDATA_IMMR, imm);
}
static inline void adis_vdat_imms(adis_vdata_t* d, std::uint32_t imm) {
    std::uint32_t pos = d->nelement++;
    adis_vdata_set(d, pos, ADIS_VDATA_IMMS, imm);
}

static inline void adis_vdat_imm(adis_vdata_t* d, std::uint32_t imm) {
    std::uint32_t pos = d->nelement++;
    adis_vdata_set_u32(d, pos, ADIS_VDATA_IMM, imm);
}
static inline void adis_vdat_label(adis_vdata_t* d, std::uint16_t imm16) {
    std::uint32_t pos = d->nelement++;
    adis_vdata_set_u32(d, pos, ADIS_VDATA_IMM, imm16);
}
static inline void adis_vdat_label_pcplus(adis_vdata_t* d, std::uint16_t imm16) {
    std::uint32_t pos = d->nelement++;
    adis_vdata_set_u32(d, pos, ADIS_VDATA_IMM_PCPLUS, imm16);
}
static inline void adis_vdat_label_pcminus(adis_vdata_t* d, std::uint16_t imm16) {
    std::uint32_t pos = d->nelement++;
    adis_vdata_set_u32(d, pos, ADIS_VDATA_IMM_PCMINUS, imm16);
}
static inline void adis_vdat_pimm(adis_vdata_t* d, std::uint32_t x) {
    adis_vdat_imm(d,x);
}

static inline void adis_vdat_imm_optional_32(adis_vdata_t* d, const u32optpair_t& imm) {
    std::uint32_t pos = d->nelement++;
    if (imm.has_value()) {
        const auto& [wmask, tmask] = imm.value();
        adis_vdata_set(d, pos, ADIS_VDATA_IMM, wmask);
    }
    else {
        // we encountered an invalid immediate value in the itext... need to tank the decode
        adis_vdata_set(d, pos, ADIS_VDATA_IMM, 0);
        adis_vdata_set_error(d, ADIS_ERROR_BAD_N_IMMS_IMMR);
    }
}
static inline void adis_vdat_imm_optional_64(adis_vdata_t* d, const u64optpair_t& imm) {
    std::uint32_t pos = d->nelement++;
    if (imm.has_value()) {
        const auto& [wmask, tmask] = imm.value();
        adis_vdata_set_imm64(d, pos, ADIS_VDATA_IMM64, wmask);
    }
    else {
        // we encountered an invalid immediate value in the itext... need to tank the decode
        adis_vdata_set_imm64(d, pos, ADIS_VDATA_IMM64, 0);
        adis_vdata_set_error(d, ADIS_ERROR_BAD_N_IMMS_IMMR);
    }
}
static inline void adis_vdat_simm(adis_vdata_t* d, std::int32_t simm) {
    std::uint32_t pos = d->nelement++;
    adis_vdata_set_i32(d, pos, ADIS_VDATA_SIMM, simm);
}
static inline void adis_vdat_simm_pcpage(adis_vdata_t* d, std::int32_t simm) {
    std::uint32_t pos = d->nelement++;
    adis_vdata_set_i32(d, pos, ADIS_VDATA_SIMM_PCPAGE, simm);
}
static inline void adis_vdat_simm_pcplus(adis_vdata_t* d, std::int32_t simm) {
    std::uint32_t pos = d->nelement++;
    adis_vdata_set_i32(d, pos, ADIS_VDATA_SIMM_PCPLUS, simm);
}
static inline void adis_vdat_simm_pcminus(adis_vdata_t* d, std::int32_t simm) {
    std::uint32_t pos = d->nelement++;
    adis_vdata_set_i32(d, pos, ADIS_VDATA_SIMM_PCMINUS, simm);
}
static inline void adis_vdat_imm1(adis_vdata_t* d, std::uint32_t x) {
    adis_vdat_imm(d,x);
}
static inline void adis_vdat_imm2(adis_vdata_t* d, std::uint32_t x) {
    adis_vdat_imm(d,x);
}
static inline void adis_vdat_imm5(adis_vdata_t* d, std::uint32_t x) {
    adis_vdat_imm(d,x);
}
static inline void adis_vdat_imm6(adis_vdata_t* d, std::uint32_t x) {
    adis_vdat_imm(d,x);
}
static inline void adis_vdat_imm8(adis_vdata_t* d, std::uint32_t x) {
    adis_vdat_imm(d,x);
}
static inline void adis_vdat_uimm4(adis_vdata_t* d, std::uint32_t x) {
    adis_vdat_imm(d,x);
}
static inline void adis_vdat_uimm6(adis_vdata_t* d, std::uint32_t x) {
    adis_vdat_imm(d,x);
}
static inline void adis_vdat_uimm(adis_vdata_t* d, std::uint32_t x) {
    adis_vdat_imm(d,x);
}
static inline void adis_vdat_prfop(adis_vdata_t* d, std::uint32_t x) {
    adis_vdat_imm(d, x);
}

static inline void adis_vdat_RESERVED(adis_vdata_t* d);

static inline void adis_vdat_index(adis_vdata_t* d, std::uint32_t x) {
    std::uint32_t pos = d->nelement++;
    adis_vdata_set_u32(d, pos, ADIS_VDATA_IMM, x);
}

// The AARCHMRS assembly grammar identifies the fields that form these
// operands but not the value transformations ADIS exposes to its consumers.
// Keep those decoder-local transformations together with the operand helpers.
static inline void adis_vdat_index1(adis_vdata_t* d, std::uint32_t imm5) {
    if (imm5 == 0 || imm5 == 0x10) {
        adis_vdat_RESERVED(d);
        return;
    }
    std::uint32_t shift = 0;
    while ((imm5 & (1U << shift)) == 0) {
        ++shift;
    }
    adis_vdat_index(d, imm5 >> (shift + 1));
}

static inline void adis_vdat_index2(adis_vdata_t* d, std::uint32_t imm5, std::uint32_t imm4) {
    if (imm5 == 0 || imm5 == 0x10) {
        adis_vdat_RESERVED(d);
        return;
    }
    std::uint32_t shift = 0;
    while ((imm5 & (1U << shift)) == 0) {
        ++shift;
    }
    adis_vdat_index(d, imm4 >> shift);
}

static inline void adis_vdat_const(adis_vdata_t* d, std::uint32_t x) {
    std::uint32_t pos = d->nelement++;
    adis_vdata_set_u32(d, pos, ADIS_VDATA_IMM, x);
}
//

static inline void adis_vdat_d(adis_vdata_t* d, std::uint32_t n) {
    // this is a secondary function, no increment; reach backwards...
    adis_vdat_n_or_ZR(d,n);
}
static inline void adis_vdat_n(adis_vdata_t* d, std::uint32_t n) {
    // this is a secondary function, no increment; reach backwards...
    adis_vdat_d(d,n);
}
static inline void adis_vdat_m(adis_vdata_t* d, std::uint32_t m) {
    // this is a secondary function, no increment; reach backwards...
    adis_vdat_d(d,m);
}

static inline void adis_vdat_m([[maybe_unused]] adis_vdata_t* d, [[maybe_unused]] std::uint32_t size,
                               [[maybe_unused]] std::uint32_t M, std::uint32_t m) {
    adis_vdat_m(d, m);
}

static inline void adis_vdat_Ws_plus1(adis_vdata_t* d, std::uint32_t n) {
    adis_vdat_Wn_or_WZR(d,n);
}

static inline void adis_vdat_Wt_plus1(adis_vdata_t* d, std::uint32_t n) {
    adis_vdat_Wn_or_WZR(d, adis_nreg(n+1));
}
static inline void adis_vdat_Xs_plus1(adis_vdata_t* d, std::uint32_t n) {
    adis_vdat_Xn_or_XZR(d, adis_nreg(n+1));
}
static inline void adis_vdat_Xt_plus1(adis_vdata_t* d, std::uint32_t n) {
    adis_vdat_Xn_or_XZR(d, adis_nreg(n+1));
}

static inline void adis_vdat_Vn_plus1(adis_vdata_t* d, std::uint32_t n) {
    std::uint32_t pos = d->nelement;
    // grab register type from earlier operand
    adis_vdata_enum_t vrtype = adis_vdata_get_enum(d,pos-1);
    d->nelement++;
    adis_vdata_set(d, pos, vrtype, adis_nreg(n+1));
}
static inline void adis_vdat_Vn_plus2(adis_vdata_t* d, std::uint32_t n) {
    std::uint32_t pos = d->nelement;
    // grab register type from earlier operand
    adis_vdata_enum_t vrtype = adis_vdata_get_enum(d,pos-1);
    d->nelement++;
    adis_vdata_set(d, pos, vrtype, adis_nreg(n+2));
}
static inline void adis_vdat_Vn_plus3(adis_vdata_t* d, std::uint32_t n) {
    std::uint32_t pos = d->nelement;
    // grab register type from earlier operand
    adis_vdata_enum_t vrtype = adis_vdata_get_enum(d,pos-1);
    d->nelement++;
    adis_vdata_set(d, pos, vrtype, adis_nreg(n+3));
}

//==========================================================

static inline void adis_vdat_Bt(adis_vdata_t* d, std::uint32_t x) {
    std::uint32_t pos = d->nelement++;
    adis_vdata_set(d, pos, ADIS_VDATA_VRB, x);
}

static inline void adis_vdat_B(adis_vdata_t* d) {
    std::uint32_t pos = d->nelement++;
    adis_vdata_set(d, pos, ADIS_VDATA_VRB, 0);
}
static inline void adis_vdat_H(adis_vdata_t* d) {
    std::uint32_t pos = d->nelement++;
    adis_vdata_set(d, pos, ADIS_VDATA_VRH, 0);
}
static inline void adis_vdat_S(adis_vdata_t* d) {
    std::uint32_t pos = d->nelement++;
    adis_vdata_set(d, pos, ADIS_VDATA_VRS, 0);
}
static inline void adis_vdat_D(adis_vdata_t* d) {
    std::uint32_t pos = d->nelement++;
    adis_vdata_set(d, pos, ADIS_VDATA_VRD, 0);
}
static inline void adis_vdat_Q(adis_vdata_t* d) {
    std::uint32_t pos = d->nelement++;
    adis_vdata_set(d, pos, ADIS_VDATA_VRQ, 0);
}

static inline void adis_vdat_shift(adis_vdata_t* d, std::uint32_t x) {
    std::uint32_t pos = d->nelement++;
    adis_vdata_set_u32(d, pos, ADIS_VDATA_IMM, x); //FIXME occasionally *16 scale for hw arg, not for imm6
}


static inline void adis_vdat_Cm(adis_vdata_t* d, std::uint32_t x) {
    adis_vdat_imm(d,x);
}
static inline void adis_vdat_Cn(adis_vdata_t* d, std::uint32_t x) {
    adis_vdat_imm(d,x);
}
static inline void adis_vdat_op0(adis_vdata_t* d, std::uint32_t x) {
    adis_vdat_imm(d,x);
}
static inline void adis_vdat_op1(adis_vdata_t* d, std::uint32_t x) {
    adis_vdat_imm(d,x);
}
static inline void adis_vdat_op2(adis_vdata_t* d, std::uint32_t x) {
    adis_vdat_imm(d,x);
}
static inline void adis_vdat_ZAd(adis_vdata_t* d, std::uint32_t x) { (void)d;(void)x; } //SME
static inline void adis_vdat_ZAda(adis_vdata_t* d, std::uint32_t x) { (void)d;(void)x; }
static inline void adis_vdat_ZAn(adis_vdata_t* d, std::uint32_t x) { (void)d;(void)x; }
static inline void adis_vdat_ZAt(adis_vdata_t* d, std::uint32_t x) { (void)d;(void)x; }



static inline void adis_vdat_t(adis_vdata_t* d, std::uint32_t x) {
    // GPR W/X or {w,}ZR(if x==31) // secondary function
    std::uint32_t pos = d->nelement-1;
    adis_vdat_table_cleanup(d,pos);
    if (x < 31)
        adis_vdata_set_id(d, pos, x);
    else { // zero reg
        adis_vdata_enum_t vrtype = adis_vdata_get_enum(d,pos);
        if (vrtype == ADIS_VDATA_WREG)
            vrtype = ADIS_VDATA_WZR;
        else if (vrtype == ADIS_VDATA_XREG)
            vrtype = ADIS_VDATA_XZR;
        else
            std::abort(); // should not happen
        adis_vdata_set(d, pos, vrtype, 0);
    }
}


static inline void adis_vdat_W(adis_vdata_t* d) { // register added later
    std::uint32_t pos = d->nelement++;
    adis_vdata_set(d, pos, ADIS_VDATA_WREG, 0);
}

static inline void adis_vdat_X(adis_vdata_t* d) {  // register added later
    std::uint32_t pos = d->nelement++;
    adis_vdata_set(d, pos, ADIS_VDATA_XREG, 0);
}

static inline void adis_vdat_dn(adis_vdata_t* d, std::uint32_t x) {
    // refiner of previous fn. So no increment. // secondary function
    // Sets the register number
    adis_vdat_d(d,x);
}

static inline void adis_vdat_fbits(adis_vdata_t* d, std::uint32_t x) {
    //FIXME: 32b form must have x[5]==1
    std::uint32_t pos = d->nelement++;
    adis_vdata_set_u32(d, pos, ADIS_VDATA_IMM, 64-(x&0x3F));
}

static inline void adis_vdat_fbits(adis_vdata_t* d, std::uint32_t immh, std::uint32_t immb) {
    if (immh < 2) {
        adis_vdat_RESERVED(d);
        return;
    }
    const std::uint32_t width = immh < 4 ? 32 : (immh < 8 ? 64 : 128);
    adis_vdat_imm(d, width - ((immh << 3) | immb));
}

static inline void adis_vdat_lsb(adis_vdata_t* d, std::uint32_t x) {
    std::uint32_t pos = d->nelement++;
    adis_vdata_set_u32(d, pos, ADIS_VDATA_IMM, x);
}
static inline void adis_vdat_mask(adis_vdata_t* d, std::uint32_t x) {
    std::uint32_t pos = d->nelement++;
    adis_vdata_set_u32(d, pos, ADIS_VDATA_IMM, x);
}
static inline void adis_vdat_nzcv(adis_vdata_t* d, std::uint32_t x) {
    std::uint32_t pos = d->nelement++;
    adis_vdata_set_u32(d, pos, ADIS_VDATA_IMM, x);
}


static inline void adis_vdat_RESERVED(adis_vdata_t* d) {
    adis_vdata_set_error(d, ADIS_ERROR_BAD_OPERAND_TABLE_ENTRY);
}
static inline void adis_vdat_d_vfp(adis_vdata_t* d, std::uint32_t regno) {
    // refining previous element stored as a string
    // convert it to a properly typed vector register
    const std::uint32_t pos = d->nelement - 1;
    const adis_vdata_enum_t etype = adis_vdata_get_enum(d, pos);

    if (etype == ADIS_VDATA_STRTAB) {
        adis_vdata_enum_t out_etype = ADIS_VDATA_INVALID;
        const adis_strtab_enum_t str_enum = adis_vdata_element_get_str_enum(d->elements + pos);
        if (str_enum == ADIS_STRTAB_S)
            out_etype = ADIS_VDATA_VRS;
        else if (str_enum == ADIS_STRTAB_D)
            out_etype = ADIS_VDATA_VRD;
        else if (str_enum == ADIS_STRTAB_B)
            out_etype = ADIS_VDATA_VRB;
        else if (str_enum == ADIS_STRTAB_H)
            out_etype = ADIS_VDATA_VRH;
        else {
            std::ostringstream os;
            os << "unexpected string in unrefined register specifier: " << adis_strtab_enum2str(str_enum);
            throw std::runtime_error(os.str());
        }
        adis_vdata_set(d, pos, out_etype, regno);
    }
    else {
        throw std::runtime_error("unexpected adis_vdata_enum_t value");
    }
}
static inline void adis_vdat_n_vfp(adis_vdata_t* d, std::uint32_t regno) {
    adis_vdat_d_vfp(d, regno);
}
static inline void adis_vdat_dn_vfp(adis_vdata_t* d, std::uint32_t regno) {
    adis_vdat_d_vfp(d, regno);
}

}  // namespace
