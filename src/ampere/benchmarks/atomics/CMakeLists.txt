# Copyright (c) 2024-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

add_benchmark(bm_caspal AARCH_ONLY CPU_FEATURE lse SOURCES test_caspal.cc)
target_compile_options(bm_caspal PRIVATE -O3 -march=armv8.5-a+lse)
