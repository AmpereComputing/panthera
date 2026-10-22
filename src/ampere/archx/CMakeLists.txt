# Copyright (c) 2018-2026 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

cmake_minimum_required(VERSION 3.11)

include(cmake/dectab.cmake)
include(cmake/xlat.cmake)

dectab(PATTERNS ${CMAKE_CURRENT_SOURCE_DIR}/branch_patterns FUNCTION decode_branch OUTPUT decode_branch.cc)
dectab(PATTERNS ${CMAKE_CURRENT_SOURCE_DIR}/address_translation_patterns FUNCTION decode_address_translation OUTPUT decode_address_translation.cc)
dectab(PATTERNS ${CMAKE_CURRENT_SOURCE_DIR}/exception_patterns FUNCTION decode_exception OUTPUT decode_exception.cc)
dectab(PATTERNS ${CMAKE_CURRENT_SOURCE_DIR}/divide_patterns FUNCTION decode_divide OUTPUT decode_divide.cc)

set(JSON_A64_XLAT ${PROJECT_SOURCE_DIR}/src/ampere/decoders/json/a64.xlat)

build_xlat_decoder(
        CPU furiosa
        XLAT_FILES ${JSON_A64_XLAT} branch.xlat float.xlat integer.xlat memory.xlat system.xlat vector.xlat fusion.xlat
        YAML_FILES exp_uops.yaml fsu_uops.yaml idr_uops.yaml ixu_uops.yaml lsu_uops.yaml unit_defaults.yaml
)

build_xlat_decoder(
        CPU ripley
        XLAT_FILES ${JSON_A64_XLAT} branch.xlat float.xlat integer.xlat memory.xlat system.xlat vector.xlat fusion.xlat
        YAML_FILES fsu_uops.yaml idr_uops.yaml ixu_uops.yaml lsu_uops.yaml unit_defaults.yaml
)

build_xlat_decoder(
        CPU trinity
        XLAT_FILES ${JSON_A64_XLAT} branch.xlat float.xlat integer.xlat memory.xlat system.xlat vector.xlat fusion.xlat
        YAML_FILES fsu_uops.yaml idr_uops.yaml ixu_uops.yaml lsu_uops.yaml unit_defaults.yaml
)

# fusion.xlat must be last to pick up references to instructions being fused
build_xlat_decoder(
        CPU connor
        XLAT_FILES ${JSON_A64_XLAT} branch.xlat float.xlat integer.xlat memory.xlat system.xlat vector.xlat
                   sve.xlat sve2.xlat memory_sve.xlat memory_sve2.xlat fusion.xlat
        YAML_FILES fsu_uops.yaml idr_uops.yaml ixu_uops.yaml lsu_uops.yaml unit_defaults.yaml
)

# Note that ConnorAI uses fsu_uops.ai_cpu.yaml instead of fsu_uops.yaml
# The decoder build (in uop.py) just pulls in *.yaml, so the big-core fsu_uops.yaml must not be present
build_xlat_decoder(
        AI CPU connor_ai
        XLAT_FILES ${JSON_A64_XLAT} branch.xlat float.xlat integer.xlat memory.xlat system.xlat vector.xlat
                   sve.xlat sve2.xlat memory_sve.xlat memory_sve2.xlat fusion.xlat
        YAML_FILES fsu_uops.ai_cpu.yaml idr_uops.yaml ixu_uops.yaml lsu_uops.yaml unit_defaults.yaml
)

build_xlat_decoder(
        CPU fusion_delay_ripley
        PARENT_CPU ripley
        XLAT_FILES ${JSON_A64_XLAT} integer.xlat
)

build_xlat_encoder(
        CPU furiosa
        XLAT_FILES ${JSON_A64_XLAT} branch.xlat float.xlat integer.xlat memory.xlat system.xlat vector.xlat fusion.xlat
)

build_xlat_encoder(
        CPU ripley
        XLAT_FILES ${JSON_A64_XLAT} branch.xlat float.xlat integer.xlat memory.xlat system.xlat vector.xlat fusion.xlat
)

build_xlat_encoder(
        CPU trinity
        XLAT_FILES ${JSON_A64_XLAT} branch.xlat float.xlat integer.xlat memory.xlat system.xlat vector.xlat fusion.xlat
)

build_xlat_encoder(
        CPU fusion_delay_ripley
        PARENT_CPU ripley
        XLAT_FILES ${JSON_A64_XLAT} integer.xlat
)

build_xlat_decoder(
        CPU test_xlat
        XLAT_FILES test.xlat
)

build_uopcode_enum(
        ${furiosa_UOPCODES}
        ${ripley_UOPCODES}
        ${trinity_UOPCODES}
        ${connor_UOPCODES}
        ${connor_ai_UOPCODES}
        ${fusion_delay_ripley_UOPCODES}
)

# Create the the archx/pmu.h file by running this above the RTL directory:
#  % sed -rn "s/^parameter CPU_PMU_([A-Z0-9_]+)\s+=\s+16'h([0-9A-F]{4});"'$'"/\1 \2/p" A0/rtl/Subsystems/Cpu/Common/PmuEvents.svh > pmu.txt
# Then copy the pmu.txt PATH-TO-PANTHERA/src/ampere/archx

add_custom_command(
  OUTPUT pmu.h
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/pmu.txt ${CMAKE_CURRENT_SOURCE_DIR}/pmu.hash.py
  COMMENT "Running the pmu enum generator..."
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/pmu.hash.py ${CMAKE_CURRENT_SOURCE_DIR}/pmu.txt)

set(SOURCES
        arch_state.cc
        arch_state.h
        branch_target.cc
        cache_maint_type.h
        decoder.cc
        decoder.h
        decoder_input.h
        decoder_mode.h
        driver_bs.cc
        driver_bs.h
        driver_is.cc
        driver_is.h
        driver.cc
        emulate.h
        emulate.cc
        fusion_opnd_check.cc
        fusion_opnd_check.h
        instruction_is.cc
        instruction_is.h
        instruction_simple.h
        instruction.cc
        inum.h
        pac_mode.h
        page_size.cc
        pmu.h
        prefetch_address.cc
        predication.cc
        predication.h
        register.cc
        tlbi.cc
        tlbi.h
        translation_debug.cc
        uop_info.h
        uop.cc
)

set(GENERATED_SOURCES
        ${CMAKE_CURRENT_BINARY_DIR}/decode_branch.cc
        ${CMAKE_CURRENT_BINARY_DIR}/decode_address_translation.cc
        ${CMAKE_CURRENT_BINARY_DIR}/decode_exception.cc
        ${CMAKE_CURRENT_BINARY_DIR}/decode_divide.cc
        ${CMAKE_CURRENT_BINARY_DIR}/uopcodes.h
        ${CMAKE_CURRENT_BINARY_DIR}/uopcodes.cc
        ${furiosa_FUNCTION_FILES}
        ${furiosa_PATTERN_INCLUDES}
        ${furiosa_ENCODER_FILES}
        ${ripley_FUNCTION_FILES}
        ${ripley_PATTERN_INCLUDES}
        ${ripley_ENCODER_FILES}
        ${trinity_FUNCTION_FILES}
        ${trinity_PATTERN_INCLUDES}
        ${trinity_ENCODER_FILES}
        ${connor_FUNCTION_FILES}
        ${connor_PATTERN_INCLUDES}
        ${connor_ENCODER_FILES}
        ${connor_ai_FUNCTION_FILES}
        ${connor_ai_PATTERN_INCLUDES}
        ${connor_ai_ENCODER_FILES}
        ${fusion_delay_ripley_FUNCTION_FILES}
        ${fusion_delay_ripley_PATTERN_INCLUDES}
        ${fusion_delay_ripley_ENCODER_FILES}
        ${test_xlat_FUNCTION_FILES}
        ${test_xlat_PATTERN_INCLUDES})

add_library(sc_main_stub sc_main_stub.cc)
target_link_libraries(sc_main_stub PUBLIC SystemC::systemc "-u sc_main")

add_library(archx_raw ${SOURCES} ${GENERATED_SOURCES})
target_compile_options(archx_raw PRIVATE "-Wextra" "-Wno-unused")
target_link_libraries(archx_raw PUBLIC SystemC::systemc instruction_stream branch_stream
                                       Boost::filesystem fmt-header-only elfio adis
                                       json immer effolkronium_random)
target_include_directories(archx_raw PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
set_property(TARGET archx_raw PROPERTY POSITION_INDEPENDENT_CODE ON)

if(PRECOMPILE_SOME_HEADERS)
    target_precompile_headers(archx_raw PUBLIC ${CMAKE_CURRENT_BINARY_DIR}/pmu.h)
endif()
# Increase constexpr step limit for PMU enum
if(CLANG_BUILD)
    target_compile_options(archx_raw PUBLIC "-fconstexpr-steps=4194304")
endif()

add_library(archx INTERFACE)
target_link_libraries(archx INTERFACE "$<LINK_LIBRARY:WHOLE_ARCHIVE,archx_raw>")


add_executable(archxdecode archxdecode.cc)
target_link_libraries(archxdecode archx capstone-static sc_main_stub)

add_subdirectory(test)
