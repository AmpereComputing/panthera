//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#ifndef AMPERE_TRACE_HINTS_H
#define AMPERE_TRACE_HINTS_H

/* Define the top-level immediates that are part of the HINT instruction
 * encoding globally to ensure we don't generate conflicts */
#define TRACE_HINT_NUM 0x55 // 0b1010101

#define TRACE_HINT_START 0x1               // begin tracing (transition to OFFLINE_WARMUP state)
#define TRACE_HINT_START_ONLINE_WARMUP 0x4 // start online warmup (ONLINE_WARMUP)
#define TRACE_HINT_START_MEASURE 0x3       // begin measurement section (MEASURING)
#define TRACE_HINT_STOP 0x2                // end measuring, begin cooldown (COOLING)

#ifdef __aarch64__
#define MACRO_STR(s) #s
#define HINT_CALL(name, imm)                                \
    static inline void do_hint_call_##name(const void* ptr) \
    {                                                       \
        register const void* localptr __asm__("x0") = ptr;  \
        asm volatile ("stp x0, x1, [sp, #-16]!\n" \
        "hint #" MACRO_STR(imm) "\n" \
        "ldp x0, x1, [sp], #16\n" \
        : /*no output*/ \
        :"r" (localptr));         \
    }

HINT_CALL(trace, TRACE_HINT_NUM)

static inline void hint_start_tracing()
{
    do_hint_call_trace((void*)TRACE_HINT_START);
}

static inline void hint_start_online_warmup()
{
    do_hint_call_trace((void*)TRACE_HINT_START_ONLINE_WARMUP);
}

static inline void hint_start_measuring()
{
    do_hint_call_trace((void*)TRACE_HINT_START_MEASURE);
}

static inline void hint_stop_tracing()
{
    do_hint_call_trace((void*)TRACE_HINT_STOP);
}

#define HINT_ITERATION() asm volatile("hint #0x54" : /*no output*/)

#else /* __aarch64__ */

static inline void hint_start_tracing() {}
static inline void hint_start_online_warmup() {}
static inline void hint_start_measuring() {}
static inline void hint_stop_tracing() {}
#define HINT_ITERATION()

#endif /* ! __aarch64__ */

#endif /* AMPERE_TRACE_HINTS_H */

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
