# Copyright (c) 2022-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

cmake_minimum_required(VERSION 3.11)

set(SOURCES compressed_fstream.cc)

add_library(util ${SOURCES})
set_property(TARGET util PROPERTY POSITION_INDEPENDENT_CODE ON)
set_property(TARGET util PROPERTY PUBLIC_HEADER arrays.h compressed_fstream.h enum.h strings.h)

target_link_libraries(util PRIVATE Boost::iostreams Boost::filesystem Boost::process INTERFACE fmt json Boost::boost)
if (${CMAKE_CXX_COMPILER_VERSION} VERSION_LESS "9")
  # gcc8 requires explicit mention of stdc++fs. (gcc9 onwwards does not)
  target_link_libraries(util INTERFACE stdc++fs)
endif()

install(TARGETS util
        ARCHIVE DESTINATION lib
        PUBLIC_HEADER DESTINATION include/ampere/util)

add_subdirectory(test)
