# Copyright (c) 2024 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

set(TESTS
    test_is_writer.cc
    test_is_reader.cc)

add_cpp_test(instruction_stream_unit SOURCES ${TESTS})
target_link_libraries(instruction_stream_unit instruction_stream)
target_compile_options(instruction_stream_unit PRIVATE -Wno-array-bounds -Wno-sign-compare)

add_python_test(instruction_stream_python_test DISCOVER ${CMAKE_CURRENT_SOURCE_DIR})
