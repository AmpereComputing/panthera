//
// Copyright (c) 2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#pragma once

#include "ampere/archx/driver.h"
#include "ampere/archx/bitfield.h"

namespace ampere::archx {

class PACMode
{
public:
    DriverPort driver_port;
    PACMode() : driver_port(nullptr) {}

    const SysReg& get_sctlr_for_el() const
    {
        switch (driver_port->exception_level()) {
        case EL::EL0: // fallthru
        case EL::EL1:
            return SCTLR_EL1;
        case EL::EL2:
            return SCTLR_EL2;
        case EL::EL3:
            return SCTLR_EL2;
        default:
            throw std::runtime_error("pac_mode.h: need a valid exception level");
        }
        return SCTLR_EL1; /* NOTREACHED */
    }

    Bitfield get_sctlr() const
    {
        const SysReg& sctlr_elx = get_sctlr_for_el();
        std::uint64_t value = driver_port->read_sysreg(sctlr_elx);
        return Bitfield(value);
    }
    bool pac_ia() const
    {
        Bitfield bf = get_sctlr();
        return bf.extract_range(31, 31) == 1;
    }

    bool pac_ib() const
    {
        Bitfield bf = get_sctlr();
        return bf.extract_range(30, 30) == 1;
    }

    bool pac_da() const
    {
        Bitfield bf = get_sctlr();
        return bf.extract_range(27, 27) == 1;
    }

    bool pac_db() const
    {
        Bitfield bf = get_sctlr();
        return bf.extract_range(13, 13) == 1;
    }
};

} // namespace ampere::archx
