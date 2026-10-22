//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//


#include "adis-vdat.h"

namespace adis {

void adis_vdat_table_cleanup(adis_vdata_t* d, std::uint32_t pos) {
    // the table functions are dumb and do not know that they are
    // setting strings for register types. Secondary functions know
    // and know how to convert the strings that the table create into
    // WREG, XREG, VRH, VRS, or VRD.

    // If it is not a ADIS_VDATA_STRTAB, then we assume that the
    // proper ADIS_VDATA_whatever was inserted.

    if (adis_vdata_get_enum(d,pos) == ADIS_VDATA_STRTAB) {
        const adis_strtab_enum_t etype = adis_vdata_get_str_enum(d,pos);
        switch (etype) {
        case ADIS_STRTAB_W: adis_vdata_set_enum(d, pos, ADIS_VDATA_WREG); break;  //FIXME: GPR vs W-element-size V-reg?
        case ADIS_STRTAB_X: adis_vdata_set_enum(d, pos, ADIS_VDATA_XREG); break;
#if defined(ADIS_UNFILTERED)
        case ADIS_STRTAB_V: adis_vdata_set_enum(d, pos, ADIS_VDATA_VREG); break;
#endif
        case ADIS_STRTAB_B: adis_vdata_set_enum(d, pos, ADIS_VDATA_VRB); break;
        case ADIS_STRTAB_H: adis_vdata_set_enum(d, pos, ADIS_VDATA_VRH); break;
        case ADIS_STRTAB_S: adis_vdata_set_enum(d, pos, ADIS_VDATA_VRS); break;
        case ADIS_STRTAB_D: adis_vdata_set_enum(d, pos, ADIS_VDATA_VRD); break;
        case ADIS_STRTAB_Q: adis_vdata_set_enum(d, pos, ADIS_VDATA_VRQ); break;
        case ADIS_STRTAB_Z: adis_vdata_set_enum(d, pos, ADIS_VDATA_ZREG); break;
        case ADIS_STRTAB_INVALID:
            adis_vdata_set_error(d, ADIS_ERROR_BAD_OPERAND_TABLE_ENTRY);
            adis_vdata_set_enum(d, pos, ADIS_VDATA_INVALID);
            break;
        default:
            throw std::runtime_error("unexpected strtab value");
        }
    }
}

} // namespace
