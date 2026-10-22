# Copyright (c) 2024 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

add_cpp_test(bobcat_integration SYSTEMC SOURCES test_bobcat.cc LABELS integration)
target_link_libraries(bobcat_integration bobcat_bpu event_stream_testing)
target_compile_options(bobcat_integration PRIVATE -Wno-sign-compare)

add_panthera_test(bobcat_fast_regress
    SPECIFICATION ${CMAKE_CURRENT_SOURCE_DIR}/fast_regress_configs.json
    WORKLOADS ${CMAKE_CURRENT_SOURCE_DIR}/../../core/test/fast_regress_workloads.json
    LABELS regression slow)
set_property(TEST bobcat_fast_regress PROPERTY COST 320)
