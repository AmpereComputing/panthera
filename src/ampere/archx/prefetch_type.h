//
// Copyright (c) 2021-2024 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once
#include <cassert>
#include <cstdint>

namespace ampere::archx {

enum class PrefetchOp {
    PLD = 0, // prefetch for load, Rt<4:3> == 0b00
    PLI = 1, // prefetch for instruction, Rt<4:3> == 0b01
    PST = 2, // prefetch for store, Rt<4:3> == 0b10
    NONE = 3
};

enum class PrefetchTarget {
    L1 = 0, // Level 1 cache, Rt<2:1> == 0b00
    L2 = 1, // Level 2 cache, Rt<2:1> == 0b01
    L3 = 2, // Level 3 cache, Rt<2:1> == 0b10
    NONE = 3
};

enum class PrefetchPolicy {
    KEEP = 0, // allocated in the cache normally, Rt<0> == 0b0
    STRM = 1, // streaming, for data that is used only once, Rt[0] == 0b1
    NONE = 2
};

inline PrefetchOp prefetch_op(std::uint32_t encoding)
{
    std::uint32_t type = (encoding >> 3) & 0x3;
    switch (type) {
    case 0b00:
        return PrefetchOp::PLD;
    case 0b01:
        return PrefetchOp::PLI;
    case 0b10:
        return PrefetchOp::PST;
    default:
        assert(false);
        return PrefetchOp::NONE;
    }
}

inline PrefetchTarget prefetch_target(std::uint32_t encoding)
{
    std::uint32_t target = (encoding >> 1) & 0x3;
    switch (target) {
    case 0b00:
        return PrefetchTarget::L1;
    case 0b01:
        return PrefetchTarget::L2;
    case 0b10:
        return PrefetchTarget::L3;
    default:
        assert(false);
        return PrefetchTarget::NONE;
    }
}

inline PrefetchPolicy prefetch_policy(std::uint32_t encoding)
{
    std::uint32_t policy = encoding & 0x1;
    switch (policy) {
    case 0b0:
        return PrefetchPolicy::KEEP;
    case 0b1:
        return PrefetchPolicy::STRM;
    default:
        assert(false);
        return PrefetchPolicy::NONE;
    }
}

} // namespace ampere::archx
