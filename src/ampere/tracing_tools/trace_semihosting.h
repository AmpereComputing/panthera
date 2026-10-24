//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#ifndef TRACE_SEMIHOSTING_H
#define TRACE_SEMIHOSTING_H

#include "semihosting.h"

#define TRACE_SEMIHOSTING_START 0x1
#define TRACE_SEMIHOSTING_STOP 0x2

#ifdef __aarch64__
SEMIHOSTING_CALL(trace, TRACE_SEMIHOSTING_NUM)

static inline void semihosting_start_tracing()
{
    do_semihosting_call_trace((void*)TRACE_SEMIHOSTING_START);
}

static inline void semihosting_stop_tracing()
{
    do_semihosting_call_trace((void*)TRACE_SEMIHOSTING_STOP);
}
#endif /* __aarch64__ */

#endif /* TRACE_SEMIHOSTING_H */

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
