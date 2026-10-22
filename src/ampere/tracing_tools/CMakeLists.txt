# Copyright (c) 2018-2026 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

cmake_minimum_required(VERSION 3.11)

if(QEMU_ROOT)
    include_directories(${QEMU_ROOT}/include)
endif()

set(BBV_GEN_SOURCES
    bbv_gen_plugin.cc
    bbv.cc
    common.cc
    context.cc
    bbv.h
    context.h
    perf_semihosting.h
    encodings.h)
add_library(bbv_generator MODULE ${BBV_GEN_SOURCES})
target_include_directories(bbv_generator PRIVATE ${GLIB_INCLUDE_DIRS})
# -DGLIB_VERSION_MIN_REQUIRED=GLIB_VERSION_2_56 is necessary to compile C++
# QEMU plugins with newer glib version
target_compile_definitions(bbv_generator PUBLIC GLIB_VERSION_MIN_REQUIRED=GLIB_VERSION_2_56)
target_link_libraries(bbv_generator PUBLIC adis fmt-header-only json ${GLIB_LDFLAGS} Boost::boost)
install(TARGETS bbv_generator DESTINATION lib)

set(INSTRUCTION_TRACER_SOURCES
    instruction_tracer.cc
    tracer.cc
    tracer.h
    common.cc
    context.cc
    context.h
    perf_semihosting.h
    encodings.h
    sveagen.cc
    sveagen.h)
add_library(instruction_tracer MODULE ${INSTRUCTION_TRACER_SOURCES})
target_include_directories(instruction_tracer PRIVATE ${GLIB_INCLUDE_DIRS})
target_compile_options(instruction_tracer PRIVATE "-Wno-unused-but-set-variable")
# -DGLIB_VERSION_MIN_REQUIRED=GLIB_VERSION_2_56 is necessary to compile C++
# QEMU plugins with newer glib version
target_compile_definitions(instruction_tracer PUBLIC GLIB_VERSION_MIN_REQUIRED=GLIB_VERSION_2_56)
target_link_libraries(instruction_tracer PUBLIC adis fmt-header-only archx branch_stream instruction_stream memory_stream elfio sc_main_stub ${GLIB_LDFLAGS} Boost::boost)
install(TARGETS instruction_tracer DESTINATION lib)

add_executable(is2bbv is2bbv.cc bbv.cc bbv.h encodings.h)
target_link_libraries(is2bbv instruction_stream adis)
install(TARGETS is2bbv DESTINATION bin)

add_executable(ms_to_l2c_warmup ms_to_l2c_warmup.cc cache.h)
target_link_libraries(ms_to_l2c_warmup fmt-header-only memory_stream json)
install(TARGETS ms_to_l2c_warmup DESTINATION bin)

add_executable(l2c_warmup_to_rtl l2c_warmup_to_rtl.cc cache.h)
target_link_libraries(l2c_warmup_to_rtl fmt-header-only memory_stream json)
install(TARGETS l2c_warmup_to_rtl DESTINATION bin)

file(COPY scripts/ DESTINATION scripts/)

add_subdirectory(test)
