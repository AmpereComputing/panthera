//
// Copyright (c) 2022-2025 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

// gcc -march=armv8.5-a -O3 basic.c -o basic -lm -static

#include <math.h>
#include <stdint.h>
#include <stdio.h>

#include "tracing_hints.h"  // copy this file from src/ampare/tracing_tools

int main() {
    uint64_t i = 0;
    float x = 0;
    hint_start_tracing();
    for (i = 0; i < 10000; i++) {
        x = x + cos((double)i / 10000.0);
    }
    hint_stop_tracing();
    printf("%lf\n", x);
}
