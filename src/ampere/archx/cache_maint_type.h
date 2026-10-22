//
// Copyright (c) 2020-2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once
#include "ampere/adis/src/adis.h"
namespace ampere::archx {

inline bool is_by_va(adis::sys_alias_t c)
{
    switch (c) {
    case adis::sys_alias_t::DC_CGDVAC:
    case adis::sys_alias_t::DC_CGDVADP:
    case adis::sys_alias_t::DC_CGDVAP:
    case adis::sys_alias_t::DC_CGVAC:
    case adis::sys_alias_t::DC_CGVADP:
    case adis::sys_alias_t::DC_CGVAP:
    case adis::sys_alias_t::DC_CIGDVAC:
    case adis::sys_alias_t::DC_CIGVAC:
    case adis::sys_alias_t::DC_CIVAC:
    case adis::sys_alias_t::DC_CVAC:
    case adis::sys_alias_t::DC_CVADP:
    case adis::sys_alias_t::DC_CVAP:
    case adis::sys_alias_t::DC_CVAU:
    case adis::sys_alias_t::DC_GVA:
    case adis::sys_alias_t::DC_GZVA:
    case adis::sys_alias_t::DC_IGDVAC:
    case adis::sys_alias_t::DC_IGVAC:
    case adis::sys_alias_t::DC_IVAC:
    case adis::sys_alias_t::DC_ZVA:
    case adis::sys_alias_t::IC_IVAU:
        return true;
    default:
        return false;
    }
}

inline bool is_by_set_way(adis::sys_alias_t c)
{
    switch (c) {
    case adis::sys_alias_t::DC_CGDSW:
    case adis::sys_alias_t::DC_CGSW:
    case adis::sys_alias_t::DC_CIGDSW:
    case adis::sys_alias_t::DC_CIGSW:
    case adis::sys_alias_t::DC_CISW:
    case adis::sys_alias_t::DC_CSW:
    case adis::sys_alias_t::DC_IGDSW:
    case adis::sys_alias_t::DC_IGSW:
    case adis::sys_alias_t::DC_ISW:
        return true;
    default:
        return false;
    }
}

inline bool is_zva(adis::sys_alias_t c)
{
    return c == adis::sys_alias_t::DC_ZVA || c == adis::sys_alias_t::DC_GZVA;
}

} // namespace ampere::archx

