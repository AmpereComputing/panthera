# Copyright (c) 2020-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/version.h.in" "${CMAKE_CURRENT_BINARY_DIR}/version.h" @ONLY)

add_library(ampere_soc_raw
            soc.cc
            soc.h
            die.cc
            die.h
            soc_for_core_pv.h
            soc_for_core_pv.cc
            soc_base.h
            warmup.cc
            warmup.h
            warmup_cache_interface.h
            warmup_info.h)

target_link_libraries(ampere_soc_raw PUBLIC ampere_core ampere_mesh ampere_sc archx)
add_library(ampere_soc INTERFACE)
target_link_libraries(ampere_soc INTERFACE "$<LINK_LIBRARY:WHOLE_ARCHIVE,ampere_soc_raw>")

add_subdirectory(test)
