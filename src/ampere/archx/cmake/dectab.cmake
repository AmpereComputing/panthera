# Copyright (c) 2019-2024 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

function(dectab)
    cmake_parse_arguments(dectab "" "PATTERNS;FUNCTION;OUTPUT" "" ${ARGN})

    if (NOT DEFINED dectab_FUNCTION)
        set(dectab_FUNCTION insn_decode)
    endif()

    if (NOT DEFINED dectab_PATTERNS)
        message(SEND_ERROR "no pattern file was given")
    endif()

    if (NOT DEFINED dectab_OUTPUT)
        get_filename_component(pattern_file_basename ${dectab_PATTERNS} NAME)
        set(dectab_OUTPUT "${pattern_file_basename}.cc.inc")
    endif()

    add_custom_command(
        OUTPUT ${dectab_OUTPUT}
        COMMAND dectab -f ${dectab_FUNCTION} ${dectab_PATTERNS} > ${CMAKE_CURRENT_BINARY_DIR}/${dectab_OUTPUT}
        DEPENDS ${dectab_PATTERNS} dectab
    )
endfunction()
