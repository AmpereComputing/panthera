# Copyright (c) 2023-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

set(TESTS
    test_chp.cc
    test_memory.cc
    test_bandwidth_latency.cc)

add_cpp_test(ampere_memory_unit SYSTEMC SOURCES ${TESTS})
target_link_libraries(ampere_memory_unit ampere_memory ampere_sc_testing)
