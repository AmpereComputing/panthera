//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once
#include <cstdint>
#include <stdexcept>

#include "adis.h"

namespace adis {

class vdata_iter_t {

    adis_vdata_t vdis;
    std::uint_fast32_t vcount;

 public:

    vdata_iter_t(std::uint32_t itext, std::uint32_t inum);

    const adis_vdata_element_t& get() const {
        return vdis.get(vcount);
    }
    void advance() {
        vcount++;
    }

    bool done() const {
        return vcount >= vdis.nelement;
    }

}; // vdata_iter_t
 
} // adis namespace
