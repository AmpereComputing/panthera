//
// Copyright (c) 2023-2025 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
// g++ -march=armv8.7-a bf16.cc -std=c++17
#include <fmt/core.h>
#include <arm_bf16.h>
#include <arm_neon.h>
#include <random>
#include <limits>

#include <sys/mman.h>
#include <cerrno>
#include <cstdio>
#include <cstdint>
#include "ampere/benchmarks/common/ops.h"

extern "C" {
#include "ampere/tracing_tools/tracing_hints.h"
#include "sve-exception.h"
}

// #define DEBUG_PREAMBLE
#if defined(DEBUG_PREAMBLE)
#include "procmaps.h"
// This was an experiment to see if the tracer could find a blank
// page to use for the preamble but it didn't work. Holding on to
// the code for now because we have a problem with the tracer
// finding a good address for the preamble it emits. Maybe this
// will help future debugging.
void allocate_emtpy_page_for_tracer_preamble()
{
    void* [[maybe_unused]] blank_exec_page =
        mmap((void*)0, 64 * 1024, PROT_READ | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    std::uint64_t va = reinterpret_cast<std::uint64_t>(blank_exec_page);
    if (va == std::numeric_limits<std::uint64_t>::max()) {
        std::perror("mmap() failed");
        exit(1);
    } else {
        fmt::print("mmap page va: {:016x}\n", va);
    }
}
#endif

static std::mt19937 gen32;
typedef union
{
    float32x4_t f32v;
    std::uint32_t u32[4];
} vec_f32_t;

void random_f32x4(float32x4_t* c, size_t nmax)
{
    for (unsigned n = 0; n < nmax; n++) {
        vec_f32_t out;
        for (unsigned lane = 0; lane < 4; lane++) {
            out.u32[lane] = gen32();
        }
        c[n] = out.f32v;
    }
}

typedef union
{
    bfloat16_t bf16;
    std::uint16_t u16;
} u_t;

typedef union
{
    bfloat16x8_t bf16v;
    std::uint16_t u16[8];
} vec_bf16_t;

void random_bf16x8(bfloat16x8_t* c, size_t nmax)
{
    for (unsigned n = 0; n < nmax; n++) {
        vec_bf16_t out;
        for (unsigned lane = 0; lane < 8; lane++) {
            out.u16[lane] = gen32();
        }
        c[n] = out.bf16v;
    }
}

int main()
{
#if defined(DEBUG_PREAMBLE)
    allocate_emtpy_page_for_tracer_preamble();
    read_proc_self_maps();
#endif
    //__bf16  x = vcvth_bf16_f32(0);
    // bfloat16x8_t y = vdupq_n_bf16(x);
    // float32x4_t acc = vdupq_n_f32(0);
    // float32x4_t z = vbfmmlaq_f32(acc, y, y);
    // float32x4_t d = vbfdotq_f32(acc, y, y);
    // float32x4_t mb = vbfmlalbq_f32(acc, y, y);
    // float32x4_t mt = vbfmlaltq_f32(acc, y, y);

    constexpr size_t M = 128;
    constexpr size_t N = 128;
    constexpr size_t K = 128;
    float32x4_t c[M * N / 4];   // M rows, N/4 cols
    bfloat16x8_t a[M * K / 8];  // M rows, K/8 cols
    bfloat16x8_t bt[N * K / 8]; // b=K rows, N/8 cols -> bt N rows, K/8 cols
    take_sve_state_exception();
    hint_start_tracing(); // begin capturing offline warmup
    random_f32x4(c, M * N / 4);
    random_bf16x8(a, M * K / 8);
    random_bf16x8(bt, M * K / 8);
    hint_start_online_warmup();
    hint_start_measuring();
    for (unsigned m = 0; m < M; m++) {
        for (unsigned n = 0; n < N / 4; n++) {
            for (unsigned k = 0; k < K / 8; k++) {
                c[m * N / 4 + n] = vbfdotq_f32(c[m * N / 4 + n], a[m * (K / 8) + k], bt[n * (K / 8) + k]);
            }
        }
    }
    hint_stop_tracing();

    pad_execution();

    // print it out to avoid compiler removing everything
    for (unsigned m = 0; m < M; m++) {
        for (unsigned n = 0; n < N / 4; n++) {
            vec_f32_t out;
            out.f32v = c[m * N / 4 + n];
            for (unsigned lane = 0; lane < 4; lane++) {
                fmt::print("{} {} {} {}\n", m, n, lane, out.u32[lane]);
            }
        }
    }
}
