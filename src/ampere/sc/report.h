//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include <fmt/format.h>
#include <fmt/ostream.h>
#include <systemc>
#include <stdexcept>

#ifndef AMPERE_SC_MESSAGE_TYPE
#define AMPERE_SC_MESSAGE_TYPE "/ampere/panthera"
#endif

#define AMPERE_FMT_STR(format_str, ...) \
    fmt::format(                        \
        "{}:{}:{}: {}\n", __FILE__, __LINE__, __PRETTY_FUNCTION__, fmt::format(format_str __VA_OPT__(, ) __VA_ARGS__))
#define AMPERE_FMT(format_str, ...) AMPERE_FMT_STR(format_str, __VA_ARGS__).c_str()

// not using AMPERE_FMT on INFO type to allow more consistent column formatting
#define AMPERE_INFO(...) SC_REPORT_INFO(AMPERE_SC_MESSAGE_TYPE, fmt::format(__VA_ARGS__).c_str())
#define AMPERE_WARNING(...) SC_REPORT_WARNING(AMPERE_SC_MESSAGE_TYPE, AMPERE_FMT(__VA_ARGS__))
#define AMPERE_ERROR(...) SC_REPORT_ERROR(AMPERE_SC_MESSAGE_TYPE, AMPERE_FMT(__VA_ARGS__))

#define AMPERE_FATAL(...) SC_REPORT_FATAL(AMPERE_SC_MESSAGE_TYPE, AMPERE_FMT(__VA_ARGS__))

#define AMPERE_DEBUG_TYPE(TYPE, ...) \
    SC_REPORT_INFO_VERB(AMPERE_SC_MESSAGE_TYPE TYPE, AMPERE_FMT(__VA_ARGS__), sc_core::SC_DEBUG)
#define AMPERE_INFO_TYPE(TYPE, ...) SC_REPORT_INFO(AMPERE_SC_MESSAGE_TYPE TYPE, fmt::format(__VA_ARGS__).c_str())

#define AMPERE_ASSERT(expr, ...) /* NOLINT(cppcoreguidelines-avoid-do-while) */ \
    do {                                                                        \
        if (!(expr))                                                            \
            AMPERE_FATAL(__VA_ARGS__);                                          \
    } while (0)

#define AMPERE_ASSERT_ERROR(expr, ...) /* NOLINT(cppcoreguidelines-avoid-do-while) */ \
    do {                                                                              \
        if (!(expr))                                                                  \
            AMPERE_ERROR(__VA_ARGS__);                                                \
    } while (0)

namespace ampere::sc {
void report_handler(const sc_core::sc_report& rep, const sc_core::sc_actions& actions);
}

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
