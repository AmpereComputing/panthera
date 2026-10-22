# Copyright (c) 2024-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

tab2enum(TAB idr_align.tab)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/idr_align.py" DESTINATION lib/python/panthera/enums)
add_dependencies(ampere_core_raw idr_align_enum)

target_sources(ampere_core_raw PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}/idr_align.enum.h"
    "${CMAKE_CURRENT_BINARY_DIR}/idr_align.enum.cc"
    assignment.h
    assignment.cc
    idr_defs.h
    idr_defs.cc
    idr_load_balancing.h
    idr_load_balancing.cc
    idr_counter_balancing.h
    idr_counter_balancing.cc
    swizzle.h
    swizzle.cc
    throttling.h
    throttling.cc
)
