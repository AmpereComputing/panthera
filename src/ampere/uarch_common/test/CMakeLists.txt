# Copyright (c) 2023-2024 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

set(TESTS
    test_cache.cc
    test_lru.cc
    test_il1lru.cc
    test_plru.cc
    test_nmru.cc
    test_rrip.cc
    test_plru5.cc
    test_plru6.cc
    test_fifo.cc
    test_rr.cc)

add_cpp_test(uarch_common_unit SYSTEMC SOURCES ${TESTS} )
target_link_libraries(uarch_common_unit uarch_common ampere_sc_testing)
target_compile_options(uarch_common_unit PRIVATE -Wno-sign-compare)
