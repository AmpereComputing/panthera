# Copyright (c) 2023-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

set(TESTS
    test_writer.cc
    test_reader.cc)

add_cpp_test(event_stream_unit SOURCES ${TESTS})
target_link_libraries(event_stream_unit event_stream event_stream_testing)
target_compile_options(event_stream_unit PRIVATE -Wno-array-bounds)

add_python_test(event_stream_python_test DISCOVER ${CMAKE_CURRENT_SOURCE_DIR})
set_tests_properties(event_stream_python_test PROPERTIES DEPENDS event_stream_unit)
