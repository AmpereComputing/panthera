# Copyright (c) 2019-2026 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

set(xlat_CMD "${PROJECT_SOURCE_DIR}/src/ampere/python/xlat/bin/xlat")
set(xlat_PYTHON_PKG "${PROJECT_SOURCE_DIR}/src/ampere/python/xlat/src")
file(GLOB_RECURSE xlat_CMD_DEPENDS "${PROJECT_SOURCE_DIR}/src/ampere/python/xlat/src/**/*.py")

function(run_xlat)
    cmake_parse_arguments(xlat "AI" "" "COMMAND;OUTPUTS;XLAT_FILES;YAML_FILES;OPTIONS;DEPENDS;PYTHON_PATH" ${ARGN})

    set(python_path ${xlat_PYTHON_PKG})

    foreach (path ${xlat_PYTHON_PATH})
        set(python_path "${python_path}:${path}")
    endforeach()

    set(env_args PYTHONPATH="${python_path}")
    if (xlat_AI)
        list(APPEND env_args AI_CPU=1)
    endif()

    add_custom_command(
        OUTPUT ${xlat_OUTPUTS}
        COMMAND env ${env_args} ${Python3_EXECUTABLE} ${xlat_COMMAND} ${xlat_OPTIONS} ${xlat_XLAT_FILES}
        DEPENDS ${xlat_XLAT_FILES} ${xlat_YAML_FILES} ${xlat_DEPENDS} ${xlat_CMD} ${xlat_CMD_DEPENDS}
    )
endfunction()


function(build_xlat_decoder)
    cmake_parse_arguments(xlat "" "CPU;PARENT_CPU" "XLAT_FILES;YAML_FILES" ${ARGN})

    if (NOT DEFINED xlat_CPU)
        message(SEND_ERROR "CPU name for decoder is not defined")
    endif()

    set(cpu_dir "${PROJECT_SOURCE_DIR}/src/ampere/decoders/${xlat_CPU}")
    set(python_path "$$PYTHONPATH:${PROJECT_SOURCE_DIR}/src/ampere/python:${cpu_dir}/lib/python3")
    if (DEFINED xlat_PARENT_CPU)
        set(parent_cpu_dir "${PROJECT_SOURCE_DIR}/src/ampere/decoders/${xlat_PARENT_CPU}")
        set(python_path "${python_path}:${parent_cpu_dir}/lib/python3")
    endif()

    foreach (xlat_file ${xlat_XLAT_FILES})
        if (IS_ABSOLUTE "${xlat_file}")
            list(APPEND xlat_files "${xlat_file}")
        else()
            list(APPEND xlat_files "${cpu_dir}/xlat/${xlat_file}")
        endif()
    endforeach()
    if (DEFINED xlat_YAML_FILES)
        foreach (yaml_file ${xlat_YAML_FILES})
            list(APPEND yaml_files "${cpu_dir}/yaml/${yaml_file}")
        endforeach()
    endif()

    set(nfiles 10)
    set(options "--cpu" ${xlat_CPU} "--fusion" --split-decoder ${nfiles})
    math(EXPR limit "${nfiles} - 1")
    foreach(indx RANGE ${limit})
       list(APPEND genfiles "${xlat_CPU}-${indx}.cc")
    endforeach()
    list(APPEND genfiles
        ${xlat_CPU}.h
        ${xlat_CPU}-int.h
        ${xlat_CPU}.cc
        ${xlat_CPU}-dec.cc)
    set(outputs ${genfiles}
                ${xlat_CPU}.patterns
                ${xlat_CPU}.fusion.patterns
                ${xlat_CPU}.inst_info.patterns
                ${xlat_CPU}.inst_info_fusion.patterns
                ${xlat_CPU}.uopcodes)

    run_xlat(
            COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/build_decoder.py"
            OUTPUTS ${outputs}
            OPTIONS ${options}
            XLAT_FILES ${xlat_files}
            YAML_FILES ${yaml_files}
            DEPENDS ${PROJECT_SOURCE_DIR}/src/ampere/python/panthera/decoder.py
                    ${PROJECT_SOURCE_DIR}/src/ampere/python/panthera/inst_info.py
                    ${PROJECT_SOURCE_DIR}/src/ampere/python/panthera/uop_info.py
            PYTHON_PATH ${python_path}
            ${xlat_UNPARSED_ARGUMENTS}
    )

    # see dectab.cmake
    dectab(PATTERNS ${CMAKE_CURRENT_BINARY_DIR}/${xlat_CPU}.patterns
           FUNCTION insn_decode)
    dectab(PATTERNS ${CMAKE_CURRENT_BINARY_DIR}/${xlat_CPU}.fusion.patterns
           FUNCTION fusion_decode)
    dectab(PATTERNS ${CMAKE_CURRENT_BINARY_DIR}/${xlat_CPU}.inst_info.patterns
           FUNCTION inst_info_decode)
    dectab(PATTERNS ${CMAKE_CURRENT_BINARY_DIR}/${xlat_CPU}.inst_info_fusion.patterns
           FUNCTION inst_info_fusion_decode)

    set(${xlat_CPU}_FUNCTION_FILES ${genfiles} PARENT_SCOPE)
    list(APPEND pattern_includes ${xlat_CPU}.patterns.cc.inc
                                 ${xlat_CPU}.fusion.patterns.cc.inc
                                 ${xlat_CPU}.inst_info.patterns.cc.inc
                                 ${xlat_CPU}.inst_info_fusion.patterns.cc.inc)

    set(${xlat_CPU}_PATTERN_INCLUDES ${pattern_includes} PARENT_SCOPE)
    set(${xlat_CPU}_UOPCODES ${xlat_CPU}.uopcodes PARENT_SCOPE)
endfunction()

function(build_xlat_encoder)
    cmake_parse_arguments(xlat "" "CPU;PARENT_CPU" "XLAT_FILES" ${ARGN})

    if (NOT DEFINED xlat_CPU)
        message(SEND_ERROR "CPU name for decoder is not defined")
    endif()

    set(cpu_dir "${PROJECT_SOURCE_DIR}/src/ampere/decoders/${xlat_CPU}")
    set(python_path "$$PYTHONPATH:${PROJECT_SOURCE_DIR}/src/ampere/python:${cpu_dir}/lib/python3")
    if (DEFINED xlat_PARENT_CPU)
        set(parent_cpu_dir "${PROJECT_SOURCE_DIR}/src/ampere/decoders/${xlat_PARENT_CPU}")
        set(python_path "${python_path}:${parent_cpu_dir}/lib/python3")
    endif()

    foreach (xlat_file ${xlat_XLAT_FILES})
        if (IS_ABSOLUTE "${xlat_file}")
            list(APPEND xlat_files "${xlat_file}")
        else()
            list(APPEND xlat_files "${cpu_dir}/xlat/${xlat_file}")
        endif()
    endforeach()

    run_xlat(
            COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/build_encoder.py"
            OUTPUTS ${xlat_CPU}_encode.h ${xlat_CPU}_encode.cc
            OPTIONS "--cpu" ${xlat_CPU}
            XLAT_FILES ${xlat_files}
            DEPENDS ${PROJECT_SOURCE_DIR}/src/ampere/python/panthera/encoder.py
            PYTHON_PATH ${python_path}
            ${xlat_UNPARSED_ARGUMENTS}
    )

    set(${xlat_CPU}_ENCODER_FILES ${xlat_CPU}_encode.h ${xlat_CPU}_encode.cc PARENT_SCOPE)
endfunction()

function(build_uopcode_enum)
    add_custom_command(
        OUTPUT uopcodes.h uopcodes.cc
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/build_uopcodes.py ${ARGN}
        DEPENDS ${ARGN} ${CMAKE_CURRENT_SOURCE_DIR}/build_uopcodes.py
    )
endfunction(build_uopcode_enum)
