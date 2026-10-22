# Copyright (c) 2022-2024 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

cmake_minimum_required(VERSION 3.11)

add_executable(bs_chop bs_chop.cc)
target_link_libraries(bs_chop branch_stream)

install(TARGETS bs_chop DESTINATION bin)

