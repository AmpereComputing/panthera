# Copyright (c) 2019-2024 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/version.h.in" "${CMAKE_CURRENT_BINARY_DIR}/version.h" @ONLY)

add_library(bobcat_bpu bobcat_bpu.cc bobcat_bpu.h)
target_link_libraries(bobcat_bpu PUBLIC ampere_core ampere_sc archx fmt)

add_executable(bobcat bobcat.cc)
target_link_libraries(bobcat PRIVATE bobcat_bpu)

install(TARGETS bobcat DESTINATION bin)

add_subdirectory(test)
