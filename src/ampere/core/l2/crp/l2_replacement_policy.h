//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include "ampere/sc/sc.h"
#include "ampere/core/l2/l2c_line.h"
#include "ampere/uarch_common/indexer.h"
#include "ampere/uarch_common/memreq.h"

namespace ampere::core {

using namespace ampere::sc;
using namespace ampere::uarch_common;
using namespace ampere::core::l2;

class L2ReplacementPolicy : public Module
{
public:
    explicit L2ReplacementPolicy(sc_core::sc_module_name name) : Module(name) {}

    enum class TouchOp {
        REFILL,
        LOOKUP, // always hit for touch
        UPDATE
    };

    using Module::init;

    virtual void init(uint64_t banks, uint64_t sets, uint64_t ways, uint64_t ways_mask) = 0;
    virtual void invalidate(uint64_t bank, uint64_t set, uint64_t way) = 0;
    virtual void touch(uint64_t bank,
                       uint64_t set,
                       uint64_t way,
                       TouchOp op,
                       bool demand,
                       const Set& set_contents,
                       MemReq req = nullptr) = 0;
    virtual uint64_t victim(uint64_t bank, uint64_t set, const Set& set_contents, uint64_t way_mask) = 0;

    CacheLookup<PhysAddr> l1i_lookup{"l1i_lookup"};
    CacheLookup<PhysAddr> l1d_lookup{"l1d_lookup"};
    bool perfect_l1i_bits{false};
    bool perfect_l1d_bits{false};

protected:
    bool get_l1i(const l2::Line& line)
    {
        if (perfect_l1i_bits)
            return l1i_lookup->address_resident(line.addr);
        else
            return line.in_l1i;
    }

    bool get_l1d(const l2::Line& line)
    {
        if (perfect_l1d_bits)
            return l1d_lookup->address_resident(line.addr);
        else
            return line.in_l1d;
    }
};

} // namespace ampere::core

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
