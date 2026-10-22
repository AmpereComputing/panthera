# Copyright (c) 2024-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

# See https://stackoverflow.com/questions/35116327/when-g-static-link-pthread-cause-segmentation-fault-why
# for a fantastic explanation of why a simple static link with -pthread can lead to segmentation faults.
set(STATIC_LINK "-static -Wl,--whole-archive -lpthread -Wl,--no-whole-archive")

# Add a benchmark
#
# Arguments
# =========
# AARCH_ONLY: compile only for aarch
# STANDALONE: benchmark has its own main() (i.e. does not use benchmark framework)
# CPU_FEATURE: require a CPU feature (from cpuinfo) in order add to tests
# SOURCES: test sources
# ARGUMENTS: test arguments
function(add_benchmark target)
    cmake_parse_arguments(BENCHMARK "AARCH_ONLY;STANDALONE;SLOW" "CPU_FEATURE" "SOURCES;ARGUMENTS" ${ARGN})
    if(${CMAKE_SYSTEM_PROCESSOR} STREQUAL "aarch64" OR NOT ${BENCHMARK_AARCH_ONLY})
        add_executable(${target} ${BENCHMARK_SOURCES})
        target_link_libraries(${target} $<IF:$<BOOL:${BENCHMARK_STANDALONE}>,,benchmark> Boost::boost fmt ${STATIC_LINK})
        # Some micros use volatile which was heavily limited in c++20
        set_property(TARGET ${target} PROPERTY CXX_STANDARD 17)
        set(HOST_HAS_FEATURES 1)
        if(BENCHMARK_CPU_FEATURE)
            execute_process(COMMAND cat /proc/cpuinfo
                COMMAND grep -i features
                COMMAND head -n 1
                COMMAND grep -c ${BENCHMARK_CPU_FEATURE}
                OUTPUT_VARIABLE HOST_HAS_FEATURES)
        endif()
        # Only add as (runnable) test if CPU feature exists
        if(HOST_HAS_FEATURES GREATER 0 AND NOT BENCHMARK_SLOW)
            add_test(NAME ${target} COMMAND ${target} ${BENCHMARK_ARGUMENTS})
            set_property(TEST ${target} PROPERTY LABELS benchmark)
        endif()
    endif()
endfunction()
