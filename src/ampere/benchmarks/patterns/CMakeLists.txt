# Copyright (c) 2024-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

add_benchmark(bm_stride_patterns SOURCES test_stride.cc ARGUMENTS --gtest_filter=*step???Xsteps???Xops?*)
target_compile_options(bm_stride_patterns PRIVATE -O3)

add_benchmark(bm_pointer_patterns SOURCES test_pointer.cc ARGUMENTS --gtest_filter=*step???Xsteps???Xops?*)
target_compile_options(bm_pointer_patterns PRIVATE -O3)

add_benchmark(bm_graph_patterns SOURCES test_graph.cc ARGUMENTS --gtest_filter=*nodes????X*)
target_compile_options(bm_graph_patterns PRIVATE -O3)

add_benchmark(bm_list_patterns SOURCES test_list.cc ARGUMENTS --gtest_filter=*size???Xops?*)
target_compile_options(bm_list_patterns PRIVATE -O3)
