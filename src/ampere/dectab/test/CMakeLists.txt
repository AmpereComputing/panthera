# Copyright (c) 2018-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

set(TESTS
    test_parser.cc
    test_builder.cc
    test_dfa.cc
    test_table.cc)

add_cpp_test(dectab_tests SOURCES ${TESTS})
target_link_libraries(dectab_tests libdectab Boost::filesystem)
target_compile_options(dectab_tests PRIVATE -Wno-sign-compare)
