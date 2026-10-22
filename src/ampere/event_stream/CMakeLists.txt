# Copyright (c) 2018-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

cmake_minimum_required(VERSION 3.11)

protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS event_stream.proto)
protobuf_generate_python(PROTO_PY event_stream.proto)
add_custom_target(event_stream_proto_python ALL DEPENDS ${PROTO_PY})
set_source_files_properties(${PROTO_SRCS} ${PROTO_HDRS} PROPERTIES SKIP_LINTING ON)

set(HEADERS
        event_base.h
        event_stream.h
        event_stream_proto.h
        event_stream_reader.h
        event_stream_recorder.h)
set(SOURCES
        event_stream.cc
        event_stream_proto.cc
        event_stream_sink.h
        event_stream_sink.cc
        event_stream_recorder.cc)

add_library(event_stream ${SOURCES} ${PROTO_SRCS} ${PROTO_HDRS} event_stream_reader.cc event_stream_reader.h)
set_property(TARGET event_stream PROPERTY POSITION_INDEPENDENT_CODE ON)
set_property(TARGET event_stream PROPERTY PUBLIC_HEADER "${HEADERS}")
target_link_libraries(event_stream PUBLIC protobuf_utils protobuf::libprotobuf Boost::filesystem Boost::system Boost::boost fmt-header-only)
target_compile_options(event_stream PRIVATE -Wno-array-bounds)

add_executable(es_viewer event_stream_viewer.cc)
target_link_libraries(es_viewer event_stream Boost::program_options)

install(TARGETS es_viewer DESTINATION bin)
install(TARGETS event_stream
        ARCHIVE DESTINATION lib
        PUBLIC_HEADER DESTINATION include/ampere/event_stream)
install(FILES ${PROTO_HDRS} DESTINATION include/ampere/event_stream)
install(FILES ${PROTO_PY} event_stream.py DESTINATION lib/python/panthera/streams)
install(TARGETS event_stream DESTINATION lib)

add_subdirectory(testing)
add_subdirectory(test)
