# Copyright (c) 2024-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

add_compile_definitions(TEST_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

set(TESTS
    test_deadlocks.cc
    test_path.cc
    test_traffic.cc
    test_setup.cc)
add_cpp_test(lynx_unit SOURCES ${TESTS})
target_link_libraries(lynx_unit lynxlib)
target_include_directories(lynx_unit PRIVATE .. ${CMAKE_CURRENT_BINARY_DIR}/..)
add_python_test(lynx_py_test DISCOVER ${CMAKE_CURRENT_SOURCE_DIR})
