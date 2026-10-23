//
// Copyright (c) 2024-2025 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include "ampere/benchmarks/common/ops.h"
#include "ampere/tracing_tools/tracing_hints.h"
#include "sve-exception.h"

extern void TEST_FUNC();

int main(int argc, char** argv) {
    take_sve_state_exception();
    hint_start_tracing();
    hint_start_online_warmup();
    hint_start_measuring();
    TEST_FUNC();
    hint_stop_tracing();
    pad_execution();
}
