# Copyright (c) 2021-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

cmake_minimum_required(VERSION 3.11)

add_executable(es_richasm es_richasm.cc)
target_link_libraries(es_richasm event_stream adis capstone-static Boost::program_options)
target_include_directories(es_richasm PUBLIC capstone adis)

add_executable(es_same_path es_same_path.cc)
target_link_libraries(es_same_path event_stream Boost::program_options fmt json)

add_executable(es_prefetcher_validator es_prefetcher_validator.cc)
target_link_libraries(es_prefetcher_validator event_stream Boost::program_options fmt json ampere_core ampere_sc)

install(TARGETS es_same_path es_richasm es_prefetcher_validator DESTINATION bin)

add_subdirectory(test)
