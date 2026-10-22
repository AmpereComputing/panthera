# Copyright (c) 2023-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

# Add a C++ (gtest) based test
#
# Arguments
# =========
# OPTIONAL: do not run by default
# SYSTEMC: simulation-based test, use sc_main test harness instead of gtest_main
# TIMEOUT: test timeout
# SOURCES: test sources
# LABELS: test labels
function(add_cpp_test target)
    cmake_parse_arguments(CPP_TEST "OPTIONAL;SYSTEMC" "" "SOURCES;LABELS;TIMEOUT" ${ARGN})
    add_executable(${target} ${CPP_TEST_SOURCES})
    if(CPP_TEST_SYSTEMC)
        target_link_libraries(${target} test_sc_main gtest gmock)
    else()
        target_link_libraries(${target} gtest_main gmock)
    endif()
    if(NOT CPP_TEST_OPTIONAL)
        add_test(NAME ${target} COMMAND ${target} WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
        list(APPEND CPP_TEST_LABELS cpp)
        set_property(TEST ${target} PROPERTY LABELS ${CPP_TEST_LABELS})
        if(CPP_TEST_TIMEOUT)
            set_property(TEST ${target} PROPERTY TIMEOUT ${CPP_TEST_TIMEOUT})
        endif()
    endif()
    if(COMPILE_WITH_RELATIVE_PATHS)
        file(RELATIVE_PATH current_source_relative_path ${PROJECT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR})
        target_compile_definitions(${target} PRIVATE
            SOURCE_DIRECTORY="${current_source_relative_path}/.."
            MODEL_CONFIG_TREE="./src/ampere/config/model"
            TEST_FIXTURE_DIRECTORY="${current_source_relative_path}")
    else()
        target_compile_definitions(${target} PRIVATE
            SOURCE_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/.."
            MODEL_CONFIG_TREE="${PROJECT_SOURCE_DIR}/src/ampere/config/model"
            TEST_FIXTURE_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}")
    endif()
endfunction()

# Add a panthera-based test
#
# Arguments
# =========
# KEEP: keep results/outputs
# OPTIONAL: do not run by default
# PYTHON: run as python study
# DRYRUN: only emit files, don't run
# VALIDATE: run with --validate
# PARALLEL: run with NPROC processes
# FILENESS: don't run or emit files
# TIMEOUT: test timeout
# SPECIFICATION: test specification file path
# WORKLOADS: workloads file path
# LABELS: test labels
function(add_panthera_test target)
    cmake_parse_arguments(PANTHERA_TEST "KEEP;OPTIONAL;PYTHON;DRYRUN;VALIDATE;PARALLEL;FILELESS" "SPECIFICATION;WORKLOADS;TIMEOUT" "LABELS;ARGUMENTS" ${ARGN})
    if(PANTHERA_TEST_PYTHON)
        set(args ${CMAKE_INSTALL_PREFIX}/bin/panthera study ${PANTHERA_TEST_SPECIFICATION} --study ${target})
    else()
        set(args ${CMAKE_INSTALL_PREFIX}/bin/panthera cheetah --specification ${PANTHERA_TEST_SPECIFICATION} --name ${target})
    endif()
    list(APPEND args --allow-debug --remove)
    if(PANTHERA_TEST_FILELESS AND NOT PANTHERA_TEST_KEEP)
        list(APPEND args --no-jobfile-output --run printer)
    else()
        list(APPEND args --run parallel)
    endif()
    if(PANTHERA_TEST_PARALLEL)
        if(DEFINED ENV{NPROC})
            list(APPEND args --local-jobs $ENV{NPROC})
        endif()
    endif()
    if(PANTHERA_TEST_WORKLOADS)
        list(APPEND args --workloads ${PANTHERA_TEST_WORKLOADS})
    endif()
    if(PANTHERA_TEST_DRYRUN)
        list(APPEND args --dryrun)
    endif()
    if(PANTHERA_TEST_VALIDATE)
        list(APPEND args --validate)
    endif()
    if(PANTHERA_TEST_ARGUMENTS)
        list(APPEND args ${PANTHERA_TEST_ARGUMENTS})
    endif()
    if(PANTHERA_TEST_OPTIONAL)
        if(PANTHERA_TEST_KEEP)
            add_custom_target(${target} WORKING_DIRECTORY ${PROJECT_SOURCE_DIR} COMMAND ${args} COMMAND_EXPAND_LISTS)
        else()
            add_custom_target(${target} WORKING_DIRECTORY COMMAND ${args} COMMAND_EXPAND_LISTS)
        endif()
    else()
        if(PANTHERA_TEST_KEEP)
            add_test(NAME ${target} WORKING_DIRECTORY ${PROJECT_SOURCE_DIR} COMMAND ${args} COMMAND_EXPAND_LISTS)
        else()
            add_test(NAME ${target} WORKING_DIRECTORY COMMAND ${args} COMMAND_EXPAND_LISTS)
        endif()
        if(PANTHERA_TEST_TIMEOUT)
            set_property(TEST ${target} PROPERTY TIMEOUT ${PANTHERA_TEST_TIMEOUT})
        endif()
        list(APPEND PANTHERA_TEST_LABELS panthera)
        set_property(TEST ${target} PROPERTY LABELS ${PANTHERA_TEST_LABELS})
    endif()
endfunction()

# Add a python (unittest) based test
#
# Arguments
# =========
# DISCOVER: run all tests within specified directory
# SPLIT: split into separate tests for parallelism
# TIMEOUT: test timeout
# PATH: additional python path
# TESTS: python test files
# LABELS: test labels
function(add_python_test target)
    cmake_parse_arguments(PYTHON_TEST "SPLIT" "DISCOVER;TIMEOUT;PATH" "TESTS;LABELS" ${ARGN})
    set(default_path ${CMAKE_INSTALL_PREFIX}/lib/python:$ENV{PYTHONPATH})
    if(PYTHON_TEST_PATH)
        string(PREPEND PYTHON_TEST_PATH ${default_path}:)
    else()
        set(PYTHON_TEST_PATH ${default_path})
    endif()
    if(PYTHON_TEST_TESTS)
        # Fix-up test paths since unittest only really supports modules
        list(GET PYTHON_TEST_TESTS 0 first_test)
        cmake_path(GET first_test PARENT_PATH test_module_directory)
        if(test_module_directory)
            cmake_path(IS_PREFIX CMAKE_CURRENT_SOURCE_DIR "${test_module_directory}" within_source)
            if(within_source)
                # Use source directory to allow relative module import
                set(test_module_directory ${CMAKE_CURRENT_SOURCE_DIR})
            endif()
        else()
            set(test_module_directory ${CMAKE_CURRENT_SOURCE_DIR})
        endif()
        string(PREPEND PYTHON_TEST_PATH ${test_module_directory}:)
        list(TRANSFORM PYTHON_TEST_TESTS REPLACE ${test_module_directory}/ "")
        list(TRANSFORM PYTHON_TEST_TESTS REPLACE "/" ".")
        list(TRANSFORM PYTHON_TEST_TESTS REPLACE ".py" "")
    endif()
    set(cmd /usr/bin/env PYTHONPATH=${PYTHON_TEST_PATH} TEST_COLLATERAL_DIR=${TEST_COLLATERAL_DIR} ${Python3_EXECUTABLE} -m)
    set(args "")
    if(CMAKE_BUILD_TYPE_NORMALIZED STREQUAL "COVERAGE")
        list(APPEND cmd coverage run -p --rcfile=${PROJECT_SOURCE_DIR}/.coveragerc -m)
    endif()
    if($ENV{UNITTEST_OUTPUT} MATCHES "xml:*")
        string(REPLACE "xml:" "./${target}/" xml_output_path $ENV{UNITTEST_OUTPUT})
        list(APPEND cmd xmlrunner)
        list(APPEND args -o ${xml_output_path})
    else()
        list(APPEND cmd unittest)
    endif()
    if(PYTHON_TEST_DISCOVER)
        list(APPEND cmd discover ${PYTHON_TEST_DISCOVER})
    endif()
    list(APPEND cmd -v -c -b --locals)
    list(APPEND PYTHON_TEST_LABELS python)
    if(PYTHON_TEST_SPLIT)
        foreach(test ${PYTHON_TEST_TESTS})
            string(REPLACE ".py" "" test_name ${test})
            string(REPLACE "test_" "" test_name ${test_name})
            string(REPLACE "." "_" test_name ${test_name})
            set(test_name "${target}_${test_name}")
            add_test(NAME ${test_name} COMMAND ${cmd} ${args} ${test} COMMAND_EXPAND_LISTS)
            if(PYTHON_TEST_TIMEOUT)
                set_property(TEST ${test_name} PROPERTY TIMEOUT ${PYTHON_TEST_TIMEOUT})
            endif()
            set_property(TEST ${test_name} PROPERTY LABELS ${PYTHON_TEST_LABELS})
            if(CMAKE_BUILD_TYPE_NORMALIZED STREQUAL "COVERAGE")
                set_property(TEST ${test_name} PROPERTY ENVIRONMENT COVERAGE_PROCESS_START=1)
            endif()
        endforeach()
    else()
        list(APPEND args ${PYTHON_TEST_TESTS})
        add_test(NAME ${target} COMMAND ${cmd} ${args} COMMAND_EXPAND_LISTS)
        set_property(TEST ${target} PROPERTY LABELS ${PYTHON_TEST_LABELS})
        if(PYTHON_TEST_TIMEOUT)
            set_property(TEST ${target} PROPERTY TIMEOUT ${PYTHON_TEST_TIMEOUT})
        endif()
        if(CMAKE_BUILD_TYPE_NORMALIZED STREQUAL "COVERAGE")
            set_property(TEST ${target} PROPERTY ENVIRONMENT COVERAGE_PROCESS_START=1)
        endif()
    endif()
endfunction()
