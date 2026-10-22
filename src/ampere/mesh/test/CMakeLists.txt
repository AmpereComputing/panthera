# Copyright (c) 2023-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

set(TESTS
    test_credit_loop.cc
    test_home_node.cc
    test_pha.cc
    test_matrixcbusy.cc
    test_mesh.cc
    test_mesh_node.cc
    test_mesh_injector.cc
    test_pager.cc
    test_rdi.cc
    test_retry.cc
    test_sam.cc)

set(TEST_DUMMIES
    dummy_memory.cc
    dummy_memory.h
    dummy_mesh_node.cc
    dummy_mesh_node.h
    dummy_requester.cc
    dummy_requester.h
    mock_mesh_port.h)

add_cpp_test(mesh_unit SYSTEMC SOURCES ${TESTS} ${TEST_DUMMIES})
target_link_libraries(mesh_unit ampere_mesh archx memory_stream ampere_sc_testing)
