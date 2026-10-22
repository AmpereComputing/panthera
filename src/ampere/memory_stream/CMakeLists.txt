# Copyright (c) 2020-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

cmake_minimum_required(VERSION 3.11)

protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS memory_stream.proto)
protobuf_generate_python(PROTO_PY memory_stream.proto)
add_custom_target(memory_stream_proto_python ALL DEPENDS ${PROTO_PY})
# Ignore array-bounds issues with generated protobuf code
set_source_files_properties(${PROTO_SRCS} PROPERTIES COMPILE_FLAGS -Wno-array-bounds)
set_source_files_properties(${PROTO_SRCS} ${PROTO_HDRS} PROPERTIES SKIP_LINTING ON)

set(SOURCES memory_stream.cc memory_stream.cc memory_stream.h)

add_library(memory_stream ${SOURCES} ${PROTO_SRCS} ${PROTO_HDRS})
set_property(TARGET memory_stream PROPERTY POSITION_INDEPENDENT_CODE ON)
target_link_libraries(memory_stream PUBLIC protobuf_utils Boost::filesystem INTERFACE protobuf::libprotobuf)
target_compile_options(memory_stream PRIVATE -Wno-array-bounds)

add_executable(ms_viewer memory_stream_viewer.cc ${PROTO_SRCS} ${PROTO_HDRS})
target_link_libraries(ms_viewer memory_stream fmt)

install(TARGETS ms_viewer DESTINATION bin)
install(FILES ${PROTO_PY} DESTINATION lib/python/panthera/streams)

add_subdirectory(test)
