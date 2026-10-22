//
// Copyright (c) 2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include "ampere/sc/sc.h"

namespace ampere::uarch_common {

using namespace ampere::sc;
using namespace ampere::uarch_common;

class CacheReplacementPolicy : public Module
{
public:
    explicit CacheReplacementPolicy(sc_core::sc_module_name name) : Module(name) {}

    enum class TouchOp {
        REFILL,
        LOOKUP, // always hit for touch
        UPDATE
    };

    using Module::init;
    virtual void init(size_t sets, size_t ways) = 0;
    virtual void invalidate(size_t set, size_t way) = 0;
    virtual void touch(size_t set, size_t way, TouchOp op, bool nontemporal = false) = 0;
    virtual size_t victim(size_t set) = 0;
};

} // namespace ampere::uarch_common

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
