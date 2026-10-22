//
// Copyright (c) 2025 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#ifdef NDEBUG
#define AMPERE_DEBUG(...) ((void)0)
#define AMPERE_DEBUG_FUNC(...) ((void)0)
#define AMPERE_UNCATEGORIZED_DEBUG(...) ((void)0)
#else

#include "ampere/sc/module.h"
#include "ampere/sc/debug_registry.h"

#include <systemc>
#include <fmt/core.h>

namespace ampere::sc::internal {

template<typename... Args>
void str_debug_print(const char* file, int line, const char* category, const std::string& message)
{
    ::sc_core::sc_report_handler::report(
        ::sc_core::SC_INFO, category, message.c_str(), ::sc_core::SC_DEBUG, file, line);
}

template<typename... Args>
void fmt_debug_print(
    const char* file, int line, const char* category, fmt::format_string<Args...> message, Args&&... args)
{
    str_debug_print(file, line, category, fmt::format(message, std::forward<Args>(args)...));
}

template<typename... Args>
void fmt_debug_print(const Module& module,
                     const char* file,
                     int line,
                     const char* category,
                     fmt::format_string<Args...> message,
                     Args&&... args)
{
    if (const auto& registry = module.get_environment()->debug; registry.enabled(module, category, module.get_clock()))
    {
        fmt_debug_print(file, line, category, message, std::forward<Args>(args)...);
    }
}

template<typename Func, typename... Args>
void func_debug_print(
    const Module& module, const char* file, int line, const char* category, Func func, Args&&... args [[maybe_unused]])
{
    if (const auto& registry = module.get_environment()->debug; registry.enabled(module, category, module.get_clock()))
    {
        str_debug_print(file, line, category, func(std::forward<Args>(args)...));
    }
}

} // namespace ampere::sc::internal

#define AMPERE_DEBUG(category, message, ...) \
    ::ampere::sc::internal::fmt_debug_print(*this, __FILE__, __LINE__, category, message __VA_OPT__(, ) __VA_ARGS__)
#define AMPERE_DEBUG_FUNC(category, func) \
    ::ampere::sc::internal::func_debug_print(*this, __FILE__, __LINE__, category, func)
#define AMPERE_UNCATEGORIZED_DEBUG(message, ...) \
    ::ampere::sc::internal::fmt_debug_print(*this, __FILE__, __LINE__, "none", message __VA_OPT__(, ) __VA_ARGS__)

#endif

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
