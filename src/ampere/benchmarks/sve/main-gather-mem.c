//
// Copyright (c) 2023-2025 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include <stdint.h>
#include <stdio.h>
#include "ampere/benchmarks/common/ops.h"
#include "ampere/tracing_tools/tracing_hints.h"
#include "sve-exception.h"
extern void TEST_FUNC();
extern void* data_addr1();

int main(int argc, char** argv)
{
    int i = 0;
    take_sve_state_exception();
    char* p1 = (char*)data_addr1();
    hint_start_tracing();
    hint_start_online_warmup();
    // stage the data into the L1
    uint64_t a = 0;
    for (i = 0; i < 64 * 1024; i += 64) {
        a += p1[i];
    }
    hint_start_measuring();
    TEST_FUNC();
    hint_stop_tracing();
    pad_execution();
    printf("%ld\n", a);
}
