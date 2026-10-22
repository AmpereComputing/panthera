//
// Copyright (c) 2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
// /usr/local/gcc-12.1.0/bin/g++ -O3 -march=armv8.6-a+sve dblpmpi2f.c --static -o dblpmpi2f
// /usr/local/bin/qemu-aarch64 -cpu max,sve128=on a.out

#include <stdint.h>
#include "ampere/tracing_tools/tracing_hints.h"

static void work()
{
    uint64_t i = 0;
    hint_start_tracing();
    hint_start_online_warmup();
    hint_start_measuring();
    asm volatile("ptrue p1.d, ALL");
    for (i = 0; i < 1000; i++) {
        asm volatile("mul z0.d, p1/m, z0.d, z31.d");
        asm volatile("mul z1.d, p1/m, z1.d, z31.d");
        asm volatile("mul z2.d, p1/m, z2.d, z31.d");
        asm volatile("mul z3.d, p1/m, z3.d, z31.d");

        asm volatile("mul z4.d, p1/m, z4.d, z31.d");
        asm volatile("mul z5.d, p1/m, z5.d, z31.d");
        asm volatile("mul z6.d, p1/m, z6.d, z31.d");
        asm volatile("mul z7.d, p1/m, z7.d, z31.d");

        asm volatile("cpy z10.d, p1/m, x0");
        asm volatile("cpy z11.d, p1/m, x0");
        asm volatile("cpy z12.d, p1/m, x0");
        asm volatile("cpy z13.d, p1/m, x0");
    }
    hint_stop_tracing();
}

int main(int argc, char** argv)
{
    work();
    (void)argc;
    (void)argv;
}

