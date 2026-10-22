# Copyright (c) 2023-2024 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

target_sources(ampere_core_raw PRIVATE
    l2_replacement_policy.h
    random.h
    random.cc
    nru.h
    nru.cc
    rrip.h
    rrip.cc
    brrip.h
    brrip.cc
    drrip.h
    drrip.cc
    oracle.h
    oracle.cc
)
