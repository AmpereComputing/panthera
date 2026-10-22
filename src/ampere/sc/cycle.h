//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#pragma once

#include <cstdint>
#include <limits>
#include <sysc/kernel/sc_time.h>

namespace ampere::sc {

inline sc_core::sc_time operator""_ns(unsigned long long x) // "
{
    return sc_core::sc_time{(double)x, sc_core::SC_NS};
}

inline sc_core::sc_time operator""_ps(unsigned long long x) // "
{
    return sc_core::sc_time{(double)x, sc_core::SC_PS};
}

using Cycle = std::int64_t;
using CycleDelta = std::int64_t;

const auto CycleMax = std::numeric_limits<Cycle>::max();

} // namespace ampere::sc
