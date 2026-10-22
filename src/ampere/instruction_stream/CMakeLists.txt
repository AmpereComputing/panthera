# Copyright (c) 2018-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

cmake_minimum_required(VERSION 3.11)

protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS instruction_stream.proto)
protobuf_generate_python(PROTO_PY instruction_stream.proto)
add_custom_target(instruction_stream_proto_python ALL DEPENDS ${PROTO_PY})
# Ignore array-bounds issues with generated protobuf code
set_source_files_properties(${PROTO_SRCS} PROPERTIES COMPILE_FLAGS -Wno-array-bounds)
set_source_files_properties(${PROTO_SRCS} ${PROTO_HDRS} PROPERTIES SKIP_LINTING ON)

set(SOURCES instruction_stream.cc instruction_stream.h)

add_library(instruction_stream ${SOURCES} ${PROTO_SRCS} ${PROTO_HDRS})
set_property(TARGET instruction_stream PROPERTY POSITION_INDEPENDENT_CODE ON)
target_link_libraries(instruction_stream PUBLIC protobuf_utils Boost::filesystem INTERFACE protobuf::libprotobuf)

add_executable(is_viewer instruction_stream_viewer.cc disasm.cc disasm.h ${PROTO_SRCS} ${PROTO_HDRS})

target_include_directories(is_viewer PUBLIC  fmt)
target_link_libraries(is_viewer instruction_stream capstone-static adis fmt-header-only)
target_include_directories(is_viewer PUBLIC capstone adis)

find_package(ZLIB)
add_executable(is_cbpng_converter cbpng_converter.cc disasm.cc disasm.h export_instruction.h ${PROTO_SRCS} ${PROTO_HDRS})
target_include_directories(is_cbpng_converter PUBLIC  fmt)
target_link_libraries(is_cbpng_converter instruction_stream capstone-static adis fmt-header-only ZLIB::ZLIB)
target_include_directories(is_cbpng_converter PUBLIC capstone adis)

add_executable(is_info instruction_stream_info.cc ${PROTO_HDRS})
target_include_directories(is_info PUBLIC adis ${Boost_INCLUDE_DIRS})
target_link_libraries(is_info instruction_stream adis)

add_executable(is_chop instruction_stream_chop.cc ${PROTO_HDRS})
target_link_libraries(is_chop instruction_stream archx sc_main_stub)

add_executable(is_disasm is_disasm.cc disasm.cc disasm.h ${PROTO_SRCS} ${PROTO_HDRS})
target_include_directories(is_disasm PUBLIC  fmt)
target_link_libraries(is_disasm instruction_stream capstone-static adis fmt-header-only)
target_include_directories(is_disasm PUBLIC capstone adis)

install(TARGETS is_viewer is_info is_disasm is_chop is_cbpng_converter DESTINATION bin)
install(FILES ${PROTO_PY} instruction_stream.py DESTINATION lib/python/panthera/streams)

add_subdirectory(test)
