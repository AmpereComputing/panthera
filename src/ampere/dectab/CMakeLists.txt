# Copyright (c) 2018-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

add_library(libdectab parser.cc parser.h regexnode.h regexnode.cc dfa.cc dfa.h builder.cc builder.h table.h table.cc)
target_link_libraries(libdectab PUBLIC Boost::boost)
target_include_directories(libdectab PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)

add_executable(dectab main.cc)
target_link_libraries(dectab libdectab)

add_subdirectory(test)
