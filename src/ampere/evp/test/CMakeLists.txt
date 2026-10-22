# Copyright (c) 2023-2024 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

set(TESTS
    test_evp.cc
    test_frame.cc)

add_cpp_test(evp_unit SOURCES ${TESTS})
target_link_libraries(evp_unit fmt event_stream_testing)
target_compile_definitions(evp_unit PRIVATE BINDIR="${CMAKE_CURRENT_BINARY_DIR}")
target_compile_options(evp_unit PRIVATE -Wno-sign-compare)
set_tests_properties(evp_unit PROPERTIES ENVIRONMENT "EVP_CONFIG_PATH=${PROJECT_SOURCE_DIR}/src/ampere/config/events.processor")

add_dependencies(evp_unit evp)
