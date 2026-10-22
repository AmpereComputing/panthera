# Copyright (c) 2023-2026 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

add_cpp_test(test_tracing_cache_unit SOURCES test_cache.cc)
target_link_libraries(test_tracing_cache_unit archx memory_stream sc_main_stub)

add_cpp_test(test_tracing_memory_unit SOURCES test_memory.cc)
target_link_libraries(test_tracing_memory_unit archx sc_main_stub)
