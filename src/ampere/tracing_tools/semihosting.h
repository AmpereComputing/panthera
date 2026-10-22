//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#ifndef AMPERE_SEMIHOSTING_H
#define AMPERE_SEMIHOSTING_H

/* Define the top-level immediates that are part of the HLT instruction
 * encoding globally to ensure we don't generate conflicts */
#define PERF_SEMIHOSTING_NUM 0xf739  /* perf_semihosting.h */
#define TRACE_SEMIHOSTING_NUM 0x38ce /* trace_semihosting.h */

/* Call like `SEMIHOSTING_CALL(trace, TRACE_SEMIHOSTING_NUM)` to define a
 * function to generate a semihosting call for your particular use */

#ifdef __aarch64__
#define MACRO_STR(s) #s
#define SEMIHOSTING_CALL(name, imm)                                               \
    static inline void do_semihosting_call_##name(const void *ptr)                \
    {                                                                             \
        register const void *localptr __asm__("x0") = ptr;                        \
        /* The hardware can overwrite x0 on semihosting calls, so we make sure we \
         * preserve it */                                                         \
        asm volatile ("stp x0, x1, [sp, #-16]!\n" \
        "hlt #" MACRO_STR(imm) "\n" \
        "ldp x0, x1, [sp], #16\n" \
        : /*no output*/ \
        :"r" (localptr));                               \
    }
#endif /* __aarch64__ */

#endif /* AMPERE_SEMIHOSTING_H */

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
