//
// Copyright (c) 2021-2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once
#include "ampere/core/ixu.h"

namespace ampere::core {

// #define DEBUG_SCHEDULER_CREDITS
#if defined(DEBUG_SCHEDULER_CREDITS)
[[maybe_unused]] static void dbg_sch(uint64_t cycle, const Uop& uop, const std::string& s)
{
    fmt::print(stderr,
               "cyc {}: release {} {} schid {} for {:s} at {:x} allocid {} txid {}\n",
               cycle,
               s,
               uop->sch.sched.name(),
               uop->schid->get_id(),
               uop->get_uopcode(),
               uop->pc().value(),
               uop->get_alloc_id(),
               uop->get_txid());
}
#define DBG_SCH(u, s) dbg_sch(fmt_cycle(), u, s)
#else
#define DBG_SCH(u, s)
#endif

// #define DEBUG_DOUBLE_PUMP_I2F_MECHANISM
//  The ##__VA_ARGS__ is a GCC-ism to work around a limitation in when the valist is empty
//  https://en.wikipedia.org/wiki/Variadic_macro_in_the_C_preprocessor
#if defined(DEBUG_DOUBLE_PUMP_I2F_MECHANISM)
#define DBG_DPI2F(format_str, ...) fmt::print(stderr, "{}: " format_str "\n", fmt_cycle(), ##__VA_ARGS__)
#else
#define DBG_DPI2F(format_str, ...)
#endif

// #define DEBUG_BYPASSING
#if defined(DEBUG_BYPASSING)
#define DBG_BYP(format_str, ...) fmt::print(stderr, "{}: " format_str "\n", fmt_cycle(), ##__VA_ARGS__)
#else
#define DBG_BYP(format_str, ...)
#endif

// #define DEBUG_FSU_BYP
#if defined(DEBUG_FSU_BYP)
#define DBG_FBYP(format_str, ...) fmt::print(stderr, "{}: " format_str "\n", fmt_cycle(), ##__VA_ARGS__)
#else
#define DBG_FBYP(format_str, ...)
#endif

// #define DEBUG_DIV_LATENCY
#if defined(DEBUG_DIV_LATENCY)
#define DBG_DIVL(format_str, ...) fmt::print(stderr, "{}: " format_str "\n", fmt_cycle(), ##__VA_ARGS__)
#else
#define DBG_DIVL(format_str, ...)
#endif

// #define DEBUG_X2Z
#if defined(DEBUG_X2Z)
#define DBG_X2Z(format_str, ...) fmt::print(stderr, "{}: " format_str "\n", fmt_cycle(), ##__VA_ARGS__)
#else
#define DBG_X2Z(format_str, ...)
#endif

// #define DEBUG_COMPLETE
#if defined(DEBUG_COMPLETE)
#define DBG_COMPLETE(format_str, ...) fmt::print(stderr, "{}: " format_str "\n", fmt_cycle(), ##__VA_ARGS__)
#else
#define DBG_COMPLETE(format_str, ...)
#endif

// #define DEBUG_IXU
#if defined(DEBUG_IXU)
#define DBGIX(format_str, ...) fmt::print(stderr, "{}: " format_str "\n", fmt_cycle(), ##__VA_ARGS__)
#else
#define DBGIX(format_str, ...)
#endif

// #define DEBUG_ISSUE
#if defined(DEBUG_ISSUE)
#define DBGISS(format_str, ...)                                                     \
    do {                                                                            \
        const sc::Cycle n = get_cycle();                                            \
        if (n >= debug_start && n < debug_end) {                                    \
            fmt::print(stderr, "{}: " format_str "\n", fmt_cycle(), ##__VA_ARGS__); \
        }                                                                           \
    } while (0)
#else
#define DBGISS(format_str, ...)
#endif

#define DBGIXU(format_str, ...) fmt::print(stderr, "{}: " format_str "\n", fmt_cycle(), ##__VA_ARGS__)

} // namespace ampere::core
