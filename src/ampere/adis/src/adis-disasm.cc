//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <string>
#include <sstream>
#include <cassert>
#include <stdexcept>
#include <iomanip>
#include <algorithm>
#include "fmt/format.h"

#include "adis.h"
#include "adis-strtab-enum.h"
#include "adis-asm-pattern.h"
#include "adis-sysreg.h"
namespace adis {

typedef std::uint_fast32_t uint_t;

static std::uint64_t mask_off_page(const std::uint64_t x) {
    return x & ~0xFFFULL;
}

static void emit_variable(std::ostringstream& s, const adis_vdata_t& vdis, uint_t vcount,
                          std::uint64_t pcva)
{
    if (vcount >= vdis.nelement) {
        //std::cerr << "PROBLEM: " << s.str() << std::endl;
        //throw std::runtime_error("Too many variable operands");
        s << "FIXME";
        return;
    }

    const adis_vdata_element_t& element = vdis.get(vcount);

    switch(adis_vdata_element_get_enum(element)) {
    case  ADIS_VDATA_INVALID: s << "invalid"; break;
    case  ADIS_VDATA_WREG:
        s << 'W' << adis_vdata_element_get_id(element); break;
    case  ADIS_VDATA_XREG:
        s << 'X' << adis_vdata_element_get_id(element); break;
    case  ADIS_VDATA_VREG: // unsized... refined to one of the following B/H/W/D/Q
        s << 'V' << adis_vdata_element_get_id(element); break;
    case  ADIS_VDATA_VRB:  //8b elem
        s << 'B' << adis_vdata_element_get_id(element); break;
    case  ADIS_VDATA_VRH:  //16b elem
        s << 'H' << adis_vdata_element_get_id(element); break;
    case  ADIS_VDATA_VRS:  //32b elem
        s << 'S' << adis_vdata_element_get_id(element); break;
    case  ADIS_VDATA_VRD:  //64b elem
        s << 'D' << adis_vdata_element_get_id(element); break;
    case  ADIS_VDATA_VRQ:  //128b elem
        s << 'Q' << adis_vdata_element_get_id(element); break;
    case  ADIS_VDATA_ZREG:
        s << 'Z' << adis_vdata_element_get_id(element); break;
    case  ADIS_VDATA_ZRB:  // 8b elem
        s << 'Z' << adis_vdata_element_get_id(element); break;
    case  ADIS_VDATA_ZRH:  //16b elem
        s << 'Z' << adis_vdata_element_get_id(element); break;
    case  ADIS_VDATA_ZRW:  //32b elem
        s << 'Z' << adis_vdata_element_get_id(element); break;
    case  ADIS_VDATA_ZRD:  //64b elem
        s << 'Z' << adis_vdata_element_get_id(element); break;
    case  ADIS_VDATA_ZRQ:  //128b elem
        s << 'Z' << adis_vdata_element_get_id(element); break;
    case  ADIS_VDATA_ZA: // tile
        s << "ZA" << adis_vdata_element_get_id(element); break;
    case  ADIS_VDATA_ZA_indirect: // tile  specified indirectly (SME: LDR ZA[Wv:imm])
        s << "ZA?" << adis_vdata_element_get_id(element); // FIXME
        break;
    case  ADIS_VDATA_PREDREG:
    case  ADIS_VDATA_PREDREG_GOVERNING:
        s << 'p' << adis_vdata_element_get_id(element); break;
    case  ADIS_VDATA_FFR:      s << "ffr"; break;
    case  ADIS_VDATA_FLAGS:    s << "flags"; break;
    case  ADIS_VDATA_FPCR:     s << "fpcr"; break;
    case  ADIS_VDATA_FPSR:     s << "fpsr"; break;
    case  ADIS_VDATA_BT:       s << "bt"; break; //FIXME
    case  ADIS_VDATA_COND:     s << "cond"; break; //FIXME
    case  ADIS_VDATA_WSP:      s << "WSP"; break;
    case  ADIS_VDATA_SP:       s << "SP"; break;
    case  ADIS_VDATA_SP_EL0:   s << "SP_EL0"; break;
    case  ADIS_VDATA_SP_EL1:   s << "SP_EL1"; break;
    case  ADIS_VDATA_SP_EL2:   s << "SP_EL2"; break;
    case  ADIS_VDATA_SP_EL3:   s << "SP_EL3"; break;
    case  ADIS_VDATA_SPSR_EL1: s << "SPSR_EL1"; break;
    case  ADIS_VDATA_SPSR_EL2: s << "SPSR_EL2"; break;
    case  ADIS_VDATA_SPSR_EL3: s << "SPSR_EL3"; break;
    case  ADIS_VDATA_ELR_EL1:  s << "ELR_EL1"; break;
    case  ADIS_VDATA_ELR_EL2:  s << "ELR_EL2"; break;
    case  ADIS_VDATA_ELR_EL3:  s << "ELR_EL3"; break;
    case  ADIS_VDATA_PC:       s << "pc"; break;
    case  ADIS_VDATA_WZR:      s << "WZR"; break;
    case  ADIS_VDATA_XZR:      s << "XZR"; break;
    case  ADIS_VDATA_IMM:
        s << adis_vdata_element_get_u32(element); break;
    case  ADIS_VDATA_IMM_PCPLUS:
        s << fmt::format("{:#x}", pcva + 4 * adis_vdata_element_get_u32(element));
        break;
    case  ADIS_VDATA_IMM_PCMINUS:
        s << fmt::format("{:#x}", pcva - 4 * adis_vdata_element_get_u32(element));
        break;
    case  ADIS_VDATA_SIMM:
        s << adis_vdata_element_get_i32(element); break;
    case  ADIS_VDATA_SIMM_PCPLUS:
        s << fmt::format("{:#x}", pcva + 4 * adis_vdata_element_get_i32(element));
        break;
    case  ADIS_VDATA_SIMM_PCPAGE:
        s << fmt::format("{:#x}", mask_off_page(pcva) + adis_vdata_element_get_i32(element));
        break;
    case  ADIS_VDATA_SIMM_PCMINUS:
        s << fmt::format("{:#x}", pcva - 4 * adis_vdata_element_get_i32(element));
        break;
    case  ADIS_VDATA_IMM64:  // uses out-of-array storage in uimm64 field
        s << "0x" << std::hex << vdis.uimm64 << std::dec; break;
    case  ADIS_VDATA_IMMR:
        s <<  adis_vdata_element_get_u32(element); break;
    case  ADIS_VDATA_IMMS:
        s <<  adis_vdata_element_get_u32(element); break;
    case  ADIS_VDATA_AMOUNT:
        s <<  adis_vdata_element_get_id(element); break;
    case  ADIS_VDATA_EXTEND:
        s <<  adis_vdata_element_get_u32(element); break;
    case  ADIS_VDATA_SYSTEMREG:
        s << "0x" << std::hex << adis_vdata_element_get_u32(element) << std::dec;  break;

    case  ADIS_VDATA_MRS:
        s << sysreg_name_MRS(adis_vdata_element_get_u32(element));
        break;
    case  ADIS_VDATA_MSRR:
        s << sysreg_name_MSRR(adis_vdata_element_get_u32(element));
        break;
    case  ADIS_VDATA_MRRS:
        s << sysreg_name_MRRS(adis_vdata_element_get_u32(element));
        break;
    case  ADIS_VDATA_MSR_reg:
        s << sysreg_name_MSR_reg(adis_vdata_element_get_u32(element));
        break;
    case  ADIS_VDATA_MSR_imm:
        s << sysreg_name_MSR_imm(adis_vdata_element_get_u32(element));
        break;

    case  ADIS_VDATA_STRTAB:
        s << adis_strtab_enum2str(adis_vdata_element_get_str_enum(element)); break;
    default:
        abort();
    }
}


std::string disasm(std::uint32_t itext, encoding_enum_t inum, std::uint64_t vaddr) {
    extern const adis_asm_pattern_t adis_asm_patterns[];
    assert(inum < ENC_LAST);

    adis_data_t adat{itext,inum};
    adis_vdata_t vdis;
    adis_dec_detail(&adat, &vdis);

    std::ostringstream s;
    s << mnemonic(inum);

    std::uint32_t offset = asm_pattern_offset(inum);
    adis_asm_pattern_t operand_kind = adis_asm_patterns[offset++];
    uint_t vcount = 0;
    while(operand_kind.strcode != ADIS_STRTAB_LAST) {

        // invalid entries denote variables
        if (operand_kind.strcode == ADIS_STRTAB_INVALID) {
            emit_variable(s, vdis, vcount, vaddr);
            vcount++;
        }
        else { // valid entries are fixed strings
            s << adis_strtab_enum2str(operand_kind.strcode);
            if (operand_kind.strcode == ADIS_STRTAB_comma) {
                s << ' ';
            }
        }
        operand_kind = adis_asm_patterns[offset++];
    }

    switch(adis_vdata_get_error(vdis)) {
    case ADIS_ERROR_NONE:
        break;
    case ADIS_ERROR_BAD_N_IMMS_IMMR:
        s << "#ADIS:ILLEGAL-IMMS-IMMR";
        break;
    case ADIS_ERROR_BAD_OPERAND_TABLE_ENTRY:
        s << "#ADIS:RESERVED-FIELD-ERROR";
        break;
    default:
        s << "#ADIS:UNKNOWN-ERROR";
        break;
    }
    std::string o = s.str();
    std::transform(o.begin(), o.end(), o.begin(), ::tolower);
    return o;
    (void) vaddr;
}
}
