//
// Copyright (c) 2023-2026 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
// Build the sve-pmu target through Panthera's normal CMake/Make workflow.
// Do not use optimization; it removes the computation.

// ARM docs say predicates and vector regs must NOT be output registers
// https://developer.arm.com/documentation/101458/2200/Compile-and-Link/Writing-inline-SVE-assembly?lang=en

// Gnu GCC docs:
// https://gcc.gnu.org/onlinedocs/gcc-12.2.0/gcc/Extended-Asm.html#Extended-Asm
// https://gcc.gnu.org/onlinedocs/gcc-12.2.0/gcc/Simple-Constraints.html#Simple-Constraints
// https://gcc.gnu.org/onlinedocs/gcc-12.2.0/gcc/Extended-Asm.html#Clobbers-and-Scratch-Registers

#include <cstdint>
#include <fmt/core.h>
#include <fmt/format.h>
extern "C" {
#include "ampere/tracing_tools/tracing_hints.h"
}

static void f(double* x, double* a, double* b, double* c, unsigned long n)
{
    unsigned long i = 0;
    asm("whilelo p0.d, %[i], %[n]              \n\
         1:                                     \n\
         ld1d z0.d, p0/z, [%[a], %[i], lsl #3]  \n\
         ld1d z1.d, p0/z, [%[b], %[i], lsl #3]  \n\
         ld1d z2.d, p0/z, [%[c], %[i], lsl #3]  \n\
         fmla z2.d, p0/m, z0.d, z1.d            \n\
         st1d z2.d, p0, [%[x], %[i], lsl #3]    \n\
         uqincd %[i]                            \n\
         whilelo p0.d, %[i], %[n]               \n\
         b.any 1b"
        : [i] "=&r"(i)
        : "[i]"(0), [x] "r"(x), [a] "r"(a), [b] "r"(b), [c] "r"(c), [n] "r"(n)
        : "memory", "cc", "p0", "z0", "z1", "z2");
}

static void work1()
{
    constexpr std::uint64_t n = 100;
    double x[n], a[n], b[n], c[n];
    hint_start_tracing();
    hint_start_online_warmup();
    for (std::uint64_t i = 0; i < n; i++) {
        a[i] = static_cast<double>(i);
        b[i] = static_cast<double>(i);
        c[i] = static_cast<double>(i);
        x[i] = 0.0;
    }
    hint_start_measuring();
    f(x, a, b, c, n);
    hint_stop_tracing();
    for (std::uint64_t i = 0; i < n; i++) {
        fmt::print("{} ", x[i]);
    }
    fmt::print("\n");
}

int main(int argc, char** argv)
{
    work1();
    (void)argc;
    (void)argv;
}
