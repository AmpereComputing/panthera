# Copyright (c) 2024-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

add_library(test_sc_main test_sc_main.cc )
target_link_libraries(test_sc_main PUBLIC SystemC::systemc fmt gtest gmock "-u sc_main")
target_include_directories(test_sc_main PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

set(TESTS
    test_ancestor.cc
    test_broadcast.cc
    test_clock.cc
    test_connect.cc
    test_counter.cc
    test_credit.cc
    test_cx_connect.cc
    test_cx_queue.cc
    test_debug_registry.cc
    test_delay.cc
    test_delay_bw.cc
    test_delay_with_credit.cc
    test_event.cc
    test_map.cc
    test_module.cc
    test_parameter.cc
    test_pipeline.cc
    test_queue.cc
    test_queue_bw.cc
    test_queue_with_credit.cc
    test_random.cc
    test_schedule.cc
    test_submodule.cc
    test_transaction.cc
    test_zdelay.cc
    test_zdelay_with_credit.cc
    test_zqueue.cc
    test_zqueue_with_credit.cc)

add_cpp_test(ampere_sc_unit SYSTEMC SOURCES ${TESTS})
target_link_libraries(ampere_sc_unit ampere_sc ampere_sc_testing)

add_cpp_test(sc_integration OPTIONAL SYSTEMC SOURCES test_credit_integration.cc)
target_link_libraries(sc_integration ampere_sc ampere_sc_testing)
