//
// Copyright (c) 2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#pragma once

#include <stdint.h>
static void take_sve_state_exception()
{
    uint64_t vl = 0;
    uint64_t zero = 0;
    asm volatile("addvl %[vl], %[zero], #1" : /* outputs */[vl] "=r"(vl) : /* inputs */[zero] "r"(zero));
}

