# Copyright (c) 2018-2023 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

add_custom_command(
        OUTPUT decode_loads.cc
        COMMAND dectab -f decode_loads ${CMAKE_CURRENT_SOURCE_DIR}/load_patterns > ${CMAKE_CURRENT_BINARY_DIR}/decode_loads.cc
        DEPENDS load_patterns
)
add_custom_command(
        OUTPUT decode_stores.cc
        COMMAND dectab -f decode_stores ${CMAKE_CURRENT_SOURCE_DIR}/store_patterns > ${CMAKE_CURRENT_BINARY_DIR}/decode_stores.cc
        DEPENDS store_patterns
)
add_custom_command(
        OUTPUT decode_load_pairs.cc
        COMMAND dectab -f decode_load_pairs ${CMAKE_CURRENT_SOURCE_DIR}/load_pair_patterns > ${CMAKE_CURRENT_BINARY_DIR}/decode_load_pairs.cc
        DEPENDS load_pair_patterns
)

add_executable(decode_loads ${CMAKE_CURRENT_BINARY_DIR}/decode_loads.cc main.cc)
target_link_libraries(decode_loads instruction_stream protobuf_utils)


set(SOURCES ${CMAKE_CURRENT_BINARY_DIR}/decode_loads.cc ${CMAKE_CURRENT_BINARY_DIR}/decode_load_pairs.cc ${CMAKE_CURRENT_BINARY_DIR}/decode_stores.cc mem_analyzer.h mem_analyzer.cc)

add_library(mem_analyzer_lib ${SOURCES})
target_link_libraries(mem_analyzer_lib PUBLIC instruction_stream protobuf_utils Boost::program_options)

add_executable(mem_analyzer mem_analyzer_main.cc)
target_link_libraries(mem_analyzer mem_analyzer_lib)

add_subdirectory(test)
