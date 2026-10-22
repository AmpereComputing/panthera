/*
 * Copyright (c) 2024-2025 Ampere Computing. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <stdexcept>

#include <fmt/core.h>
#include <fmt/format.h>

namespace ampere::lynx {

struct Error : public std::runtime_error
{
    template<typename... Args>
    Error(const char* fmt, Args&&... args)
        : std::runtime_error{fmt::format(fmt::runtime(fmt), std::forward<Args>(args)...)}
    {
    }
};

#define AMPERE_LYNX_ERROR(NAME) \
    struct NAME : public Error  \
    {                           \
        using Error::Error;     \
    };

AMPERE_LYNX_ERROR(NoComponent)
AMPERE_LYNX_ERROR(RuleError)
AMPERE_LYNX_ERROR(DuplicateLocation)
AMPERE_LYNX_ERROR(NotImplementedByComponent)
AMPERE_LYNX_ERROR(BadConnectivity)
AMPERE_LYNX_ERROR(BadGraph)
AMPERE_LYNX_ERROR(NoRoute)
AMPERE_LYNX_ERROR(UnsupportedLocation)

} // namespace ampere::lynx
