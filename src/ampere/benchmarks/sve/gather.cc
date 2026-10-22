//
// Copyright (c) 2023-2025 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <cstdint>
#include "ampere/benchmarks/common/ops.h"
#include <arm_sve.h>
#define FMT_HEADER_ONLY
#include <fmt/core.h>

extern "C" {
#include "ampere/tracing_tools/tracing_hints.h"
#include "sve-exception.h"
}

// arm_sve_vector_bits attribute not supported by GCC13 default. Maybe a knob?
// typedef svint32_t vec_t __attribute__((arm_sve_vector_bits(128)));
// typedef svbool_t pred_t __attribute__((arm_sve_vector_bits(128)));

int main(int argc, char** argv)
{
    take_sve_state_exception();
    svbool_t p = svptrue_b32();
    double a[1000] = {0};
    uint32_t indexes[1000] = {0};
    hint_start_tracing();
    hint_start_online_warmup();
    hint_start_measuring();
    svuint64_t idx0 = svld1uw_u64(p, indexes + 0);
    svuint64_t idx1 = svld1uw_u64(p, indexes + 1);
    svuint64_t idx2 = svld1uw_u64(p, indexes + 2);
    svuint64_t idx3 = svld1uw_u64(p, indexes + 3);
    svfloat64_t o0, o1, o2, o3;
    for (int i = 0; i < 1000; i++) {
        o0 = svld1_gather_index(p, a, idx0);
        o1 = svld1_gather_index(p, a + 1, idx1);
        o2 = svld1_gather_index(p, a + 2, idx2);
        o3 = svld1_gather_index(p, a + 3, idx3);
    }
    double sum = svaddv(svptrue_b64(), o0);
    sum += svaddv(svptrue_b64(), o1);
    sum += svaddv(svptrue_b64(), o2);
    sum += svaddv(svptrue_b64(), o3);
    hint_stop_tracing();
    pad_execution();
    fmt::print("{:9.3f}", sum);
}
