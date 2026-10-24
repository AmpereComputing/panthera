# Copyright (c) 2018-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

cmake_minimum_required(VERSION 3.11)

protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS example.proto)
protobuf_generate_python(PROTO_PY example.proto)
add_custom_target(example_proto_python ALL DEPENDS ${PROTO_PY})
set_source_files_properties(${PROTO_SRCS} ${PROTO_HDRS} PROPERTIES SKIP_LINTING ON)

set(SOURCES protobuf_utils.cc protobuf_stream.cc)
set(TESTS test/test_delimited.cc)

add_library(protobuf_utils ${SOURCES} ${PROTO_SRCS} ${PROTO_HDRS})
set_property(TARGET protobuf_utils PROPERTY POSITION_INDEPENDENT_CODE ON)
set_property(TARGET protobuf_utils PROPERTY PUBLIC_HEADER protobuf_utils.h protobuf_stream.h)
target_link_libraries(protobuf_utils PUBLIC protobuf::libprotobuf Boost::filesystem Boost::process)
target_link_libraries(protobuf_utils PRIVATE util)

add_cpp_test(protobuf_utils_unit SOURCES ${TESTS})
target_link_libraries(protobuf_utils_unit protobuf_utils)

install(TARGETS protobuf_utils
        ARCHIVE DESTINATION lib
        PUBLIC_HEADER DESTINATION include/ampere/protobuf_utils)
install(FILES protobuf_utils.py protobuf_stream.py DESTINATION lib/python/panthera/streams)

add_python_test(protobuf_python_test DISCOVER ${CMAKE_CURRENT_SOURCE_DIR}/test)
