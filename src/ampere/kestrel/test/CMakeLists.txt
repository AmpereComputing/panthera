# Copyright (c) 2024-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

set(UNIT_TESTS
    test_tile.cc
)

set(INTEGRATION_TESTS
    test_slice.cc
)

set(TEST_DUMMIES
    ${CMAKE_SOURCE_DIR}/src/ampere/mesh/test/dummy_requester.cc
    ${CMAKE_SOURCE_DIR}/src/ampere/mesh/test/dummy_requester.h
    ${CMAKE_SOURCE_DIR}/src/ampere/mesh/test/mock_mesh_port.h)

add_cpp_test(kestrel_unit SYSTEMC SOURCES ${UNIT_TESTS} ${TEST_DUMMIES})
target_link_libraries(kestrel_unit ampere_kestrel ampere_memory ampere_sc_testing)

