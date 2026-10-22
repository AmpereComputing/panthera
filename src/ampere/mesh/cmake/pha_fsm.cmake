# Copyright (c) 2022-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

function(build_pha_fsm)
    cmake_parse_arguments(pha_fsm "" "XLSX;NFILES" "" ${ARGN})

    math(EXPR limit "${pha_fsm_NFILES} - 1")
    foreach(indx RANGE ${limit})
       list(APPEND genfiles "pha_fsm_${indx}.cc")
    endforeach()
    list(APPEND genfiles pha_fsm.cc pha_fsm.h pha_fsm_funcs.h)
    add_custom_command(
        OUTPUT  ${genfiles}
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/gen_pha_fsm.py ${CMAKE_CURRENT_SOURCE_DIR}/${pha_fsm_XLSX} -o pha_fsm -s ${pha_fsm_NFILES}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${pha_fsm_XLSX} ${CMAKE_CURRENT_SOURCE_DIR}/gen_pha_fsm.py
    )
endfunction(build_pha_fsm)

function(build_pha_state_fsm)
    cmake_parse_arguments(pha_state_fsm "" "XLSX" "" ${ARGN})

    add_custom_command(
        OUTPUT pha_state_fsm.cc
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/gen_pha_state_fsm.py ${CMAKE_CURRENT_SOURCE_DIR}/${pha_state_fsm_XLSX} -o pha_state_fsm.cc
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${pha_state_fsm_XLSX} ${CMAKE_CURRENT_SOURCE_DIR}/gen_pha_state_fsm.py
    )
endfunction(build_pha_state_fsm)
