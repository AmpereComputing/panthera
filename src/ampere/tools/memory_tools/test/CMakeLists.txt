# Copyright (c) 2023 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

set(TESTS test_mem_analyzer.cc)
add_cpp_test(mem_analyzer_tests SOURCES ${TESTS})
target_link_libraries(mem_analyzer_tests mem_analyzer_lib)
target_compile_options(mem_analyzer_tests PRIVATE -Wno-sign-compare)
