# Copyright (c) 2024-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

target_sources(ampere_core_raw PRIVATE
    cache.h
    cache.cc
    cache_conflicts.h
    cache_conflicts.cc
    ofb.h
    ofb.cc
    pob.h
    pob.cc
    pwc.h
    pwc.cc
    sf_predictor.h
    sf_predictor.cc
)

add_subdirectory(test)
