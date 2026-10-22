# Copyright (c) 2021-2026 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/version.h.in" "${CMAKE_CURRENT_BINARY_DIR}/version.h" @ONLY)

add_executable(amperesys sc_main.cc ${CMAKE_CURRENT_BINARY_DIR}/version.h sys.h)
target_link_libraries(amperesys  ampere_soc ampere_core ampere_kestrel ampere_mesh ampere_memory uarch_common archx)
target_compile_options(amperesys PRIVATE "-Wall" "-Werror")

install(TARGETS amperesys DESTINATION bin)

add_subdirectory(test)
