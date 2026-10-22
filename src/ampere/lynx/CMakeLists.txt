# Copyright (c) 2024-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

add_compile_options(-Wall -Werror)

tab2enum(TAB enums.tab HFILE lynx.enum.h CCFILE lynx.enum.cc)
add_custom_target(enum-headers DEPENDS lynx.enum.h)

add_custom_command(
    OUTPUT default_targets.cc
    COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/gen_targets_by_channel_and_type.py" "${CMAKE_CURRENT_SOURCE_DIR}/Domino Allowed Communication.xlsx" -o default_targets.cc
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/gen_targets_by_channel_and_type.py" "${CMAKE_CURRENT_SOURCE_DIR}/Domino Allowed Communication.xlsx"
)

set(lib_sources
    accumulator.cc
    accumulator.h
    component.cc
    component.h
    default_targets.cc
    link_graph.cc
    link_graph.h
    location.cc
    location.h
    model.cc
    model.h
    routing.cc
    routing.h
    rules.cc
    rules.h
    types.h)

set(CMAKE_SHARED_MODULE_PREFIX "")

add_library(lynxlib ${lib_sources})
target_link_libraries(lynxlib PUBLIC fmt-header-only json util)
add_dependencies(lynxlib enum-headers)
target_include_directories(lynxlib PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
target_compile_options(lynxlib PRIVATE -fPIC)

add_library(lynx MODULE pywrap.cc)
target_link_libraries(lynx PUBLIC lynxlib ${BOOST_PYTHON_COMPONENT_NAME} Python3::Python)

install(TARGETS lynx DESTINATION lib/python/panthera)
add_subdirectory(test)
