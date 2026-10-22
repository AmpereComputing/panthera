# Copyright (c) 2023-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

set(TESTS
    test_bits.cc
    test_compressed_fstream.cc
    test_enum.cc
    test_strings.cc)

add_cpp_test(util_unit SOURCES ${TESTS})
target_link_libraries(util_unit util)
