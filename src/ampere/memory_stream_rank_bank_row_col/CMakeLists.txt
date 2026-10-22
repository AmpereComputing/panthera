# Copyright (c) 2022-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

cmake_minimum_required(VERSION 3.11)

protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS memory_stream_rank_bank_row_col.proto)
protobuf_generate_python(PROTO_PY memory_stream_rank_bank_row_col.proto)
add_custom_target(memory_stream_rank_bank_row_col_proto_python ALL DEPENDS ${PROTO_PY})

set_source_files_properties(${PROTO_SRCS} ${PROTO_HDRS} PROPERTIES SKIP_LINTING ON)

set(SOURCES memory_stream_rank_bank_row_col.cc memory_stream_rank_bank_row_col.cc memory_stream_rank_bank_row_col.h)
set(SOURCE2 msrbrc_write.cc)

add_library(memory_stream_rank_bank_row_col ${SOURCES} ${PROTO_SRCS} ${PROTO_HDRS})
set_property(TARGET memory_stream_rank_bank_row_col PROPERTY POSITION_INDEPENDENT_CODE ON)
target_compile_options(memory_stream_rank_bank_row_col PRIVATE "-Wall" "-Werror")
target_link_libraries(memory_stream_rank_bank_row_col INTERFACE protobuf_utils protobuf::libprotobuf Boost::filesystem Boost::system)

add_executable(ms_viewer_rank_bank_row_col memory_stream_viewer_rank_bank_row_col.cc ${PROTO_SRCS} ${PROTO_HDRS})
target_compile_options(ms_viewer_rank_bank_row_col PRIVATE "-Wall")
target_link_libraries(ms_viewer_rank_bank_row_col memory_stream_rank_bank_row_col)

add_executable(memory_stream_rank_bank_row_col_unit ${PROTO_SRCS} ${PROTO_HDRS})
target_link_libraries(memory_stream_rank_bank_row_col_unit gtest_main memory_stream_rank_bank_row_col)
target_compile_definitions(memory_stream_rank_bank_row_col_unit PRIVATE SOURCE_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}")

install(TARGETS ms_viewer_rank_bank_row_col DESTINATION bin)
#install(TARGETS msrbrc_writer DESTINATION bin)
install(FILES ${PROTO_PY} DESTINATION lib/python/panthera)

#add_library(msrbrc_write ${SOURCES} ${PROTO_SRCS} ${PROTO_HDRS})
##set_property(TARGET msrbrc_write PROPERTY POSITION_INDEPENDENT_CODE ON)
#target_compile_options(msrbrc_write PRIVATE "-Wall" "-Werror")
#target_include_directories(msrbrc_write PUBLIC ${PROTOBUF_INCLUDE_DIRS})
#target_include_directories(msrbrc_write PUBLIC ${Boost_INCLUDE_DIRS})
#target_link_libraries(msrbrc_write INTERFACE protobuf_utils ${PROTOBUF_LIBRARIES} Boost::filesystem Boost::system)

#add_executable(msrbrc_writer msrbrc_write.cc ${PROTO_SRCS} ${PROTO_HDRS})
#target_compile_options(msrbrc_writer PRIVATE "-Wall" "-Werror")
#target_link_libraries(msrbrc_writer memory_stream_rank_bank_row_col)

