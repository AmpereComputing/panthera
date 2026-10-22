# Copyright (c) 2021-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

cmake_minimum_required(VERSION 3.11)

set(Protobuf_IMPORT_DIRS "${PROJECT_SOURCE_DIR}/src/ampere/instruction_stream")
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS branch_stream.proto)
protobuf_generate_python(PROTO_PY branch_stream.proto)
add_custom_target(branch_stream_proto_python ALL DEPENDS ${PROTO_PY})
# Ignore array-bounds issues with generated protobuf code
set_source_files_properties(${PROTO_SRCS} PROPERTIES COMPILE_FLAGS -Wno-array-bounds SKIP_LINTING ON)
set_source_files_properties(${PROTO_SRCS} ${PROTO_HDRS} PROPERTIES SKIP_LINTING ON)

set(SOURCES branch_stream.cc branch_stream.cc branch_stream.h)

add_library(branch_stream ${SOURCES} ${PROTO_SRCS} ${PROTO_HDRS})
set_property(TARGET branch_stream PROPERTY POSITION_INDEPENDENT_CODE ON)
target_link_libraries(branch_stream PUBLIC instruction_stream protobuf_utils Boost::filesystem INTERFACE protobuf::libprotobuf)
target_compile_options(branch_stream PRIVATE -Wno-array-bounds)

add_executable(bs_viewer branch_stream_viewer.cc ${PROTO_SRCS} ${PROTO_HDRS})
target_link_libraries(bs_viewer branch_stream fmt)

install(TARGETS bs_viewer DESTINATION bin)
install(FILES ${PROTO_PY} DESTINATION lib/python/panthera/streams)

add_subdirectory(test)
