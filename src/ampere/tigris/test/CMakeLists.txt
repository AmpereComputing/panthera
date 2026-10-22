# Copyright (c) 2024-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

add_cpp_test(tigris_integration SYSTEMC SOURCES test_tigris.cc LABELS integration)
target_link_libraries(tigris_integration tigris_lib ampere_sc_testing)
target_compile_options(tigris_integration PRIVATE -Wno-sign-compare)
