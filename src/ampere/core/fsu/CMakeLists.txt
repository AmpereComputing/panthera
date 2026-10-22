# Copyright (c) 2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

target_sources(ampere_core_raw PRIVATE
    bypass.h
    bypass.cc
)

add_subdirectory(test)
