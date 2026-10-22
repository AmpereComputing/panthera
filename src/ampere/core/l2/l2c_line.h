//
// Copyright (c) 2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include "ampere/archx/addr.h"
#include "ampere/uarch_common/memop.h"
#include "ampere/util/enum.h"
#include <fmt/ostream.h>

make_ns_enum(ampere::core::l2, LineState, I, SC, UC, SD, UD);
make_ns_enum(ampere::core::l2, CacheType, L2, L3);

namespace ampere::core::l2 {

using namespace archx;
using namespace ampere::uarch_common;

inline bool is_clean(LineState state)
{
    return state == LineState::SC || state == LineState::UC;
}

inline bool is_dirty(LineState state)
{
    return state == LineState::UD || state == LineState::SD;
}

inline bool is_unique(LineState state)
{
    return state == LineState::UC || state == LineState::UD;
}

struct Line
{
    Line() : addr{0_pa}, state{LineState::I}, cache_type{CacheType::L2}, prefetch{false} {}
    Line(PhysAddr addr, LineState state, CacheType cache_type, bool prefetch = false)
        : addr{addr}, state{state}, cache_type{cache_type}, prefetch{prefetch}
    {
    }

    PhysAddr addr;
    LineState state;
    CacheType cache_type;

    // Only for stats purposes, not in RTL
    MemOp::PrefetchType prefetch_type{MemOp::PrefetchType::NONE};

    bool prefetch{false};
    bool in_l1d{false};
    bool in_l1i{false};
    bool busy{false};
    uint64_t va_bits{0}; // actual number of bits is defined by num_va_bits parameter

    void set_prefetch(ampere::uarch_common::MemOp::PrefetchType type)
    {
        prefetch = true;
        prefetch_type = type;
    }
    void clear_prefetch()
    {
        prefetch = false;
        prefetch_type = ampere::uarch_common::MemOp::PrefetchType::NONE;
    }
};

struct Way
{
    uint64_t tag;
    Line line;
};
using Set = std::vector<Way>;

} // namespace ampere::core::l2
