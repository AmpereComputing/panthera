# Copyright (c) 2024-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

set(sources
    test_prfm.cc
    prefetch_code.S
    prefetch_data.S
)
add_benchmark(bm_prfm AARCH_ONLY SOURCES ${sources})
