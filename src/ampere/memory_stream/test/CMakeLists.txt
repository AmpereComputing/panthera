# Copyright (c) 2023-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

set(TESTS
    test_ms_writer.cc
    test_ms_reader.cc)

add_cpp_test(memory_stream_unit SOURCES ${TESTS})
target_link_libraries(memory_stream_unit memory_stream)
target_compile_options(memory_stream_unit PRIVATE -Wno-sign-compare -Wno-array-bounds)
