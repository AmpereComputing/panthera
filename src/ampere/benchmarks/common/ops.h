//
// Copyright (c) 2025 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#ifndef BENCHMARK_OPS_H_
#define BENCHMARK_OPS_H_

void pad_execution_for(int iterations)
{
    // dummy loop to avoid hitting system-y stuff
    int i = 0, x = 0;
    for (i = 0; i < 5000; i++) {
        asm volatile("add %w0, %w1, #3" : "=r"(x) : "r"(x));
    }
}

void pad_execution()
{
    pad_execution_for(5000);
}

#endif
