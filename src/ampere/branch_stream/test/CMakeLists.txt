# Copyright (c) 2023-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

set(TESTS
    test_bs_writer.cc
    test_bs_reader.cc)

add_cpp_test(branch_stream_unit SOURCES ${TESTS})
target_link_libraries(branch_stream_unit branch_stream)
target_compile_options(branch_stream_unit PRIVATE -Wno-sign-compare -Wno-array-bounds)
