//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "adis.h"
#include "adis-operand-iter.h"
#include "adis-decodeable-items.h" // generated

namespace adis {

vdata_iter_t::vdata_iter_t(std::uint32_t itext, std::uint32_t inum)
    : vcount(0) {
        if (inum >= ADIS_MAX_INST)
            throw std::runtime_error("vdata_iter_t: invalid inum");
        adis_data_t adat{itext,inum};
        adis_dec_detail(&adat, &vdis);
    }

 
} // adis namespace
