# Copyright (c) 2018-2026 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

cmake_minimum_required(VERSION 3.24)

project(panthera CXX C ASM)

message("C Compiler version: ${CMAKE_C_COMPILER_VERSION}")
message("C++ Compiler version: ${CMAKE_CXX_COMPILER_VERSION}")

set(CLANG_BUILD OFF)
if(CMAKE_CXX_COMPILER MATCHES "^.*clang.*")
    set(CLANG_BUILD ON)
endif()

if(CMAKE_BUILD_TYPE)
    string(TOUPPER ${CMAKE_BUILD_TYPE} CMAKE_BUILD_TYPE_NORMALIZED)
else()
    set(CMAKE_BUILD_TYPE_NORMALIZED RELEASE)
endif()

# Protect from rerunning configure in different environment configurations.
# CMake caches compilers and they will be wrong.
set(USE_NIX $ENV{IN_NIX_SHELL} CACHE STRING "Run in Nix Environment")
string(COMPARE NOTEQUAL "${USE_NIX}" "$ENV{IN_NIX_SHELL}" NIX_SHELL_NOT_MATCH)
if(${NIX_SHELL_NOT_MATCH})
    message(FATAL_ERROR "Using nix shell when cmake was configured without it or vice versa, USE_NIX=${USE_NIX}, IN_NIX_SHELL=$ENV{IN_NIX_SHELL}")
endif()

cmake_host_system_information(RESULT X86 QUERY HAS_MMX)

# 'find' git prior to including GetGitRevisionDescription. The automatic
# submodule updating logic requires a recent git, but
# GetGitRevisionDescription will query git internally with no minimum version
# specified and cmake will then cache the result. Doing our query (with a
# specific git version) before calling GetGitRevisionDescription ensures our
# desired git version is cached instead.
find_package(Git 2.25.0 REQUIRED)

# Add CMAKE Module path
set(CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/src/cmake-external")
include(BuildType)
include(GetGitRevisionDescription)

get_git_head_revision(AMPERE_CORE_GIT_REF AMPERE_CORE_GIT_SHA)
git_local_changes(AMPERE_CORE_GIT_STATE)

# Install into the install subdirectory
set(CMAKE_INSTALL_MESSAGE LAZY)
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
  set(CMAKE_INSTALL_PREFIX "${CMAKE_SOURCE_DIR}/install" CACHE PATH "default install path" FORCE )
endif()

set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

# Ensure std:c++ is used rather than std:gnu++
set(CMAKE_C_STANDARD 23)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Allow code to be more readable when debugging
if(NOT CLANG_BUILD)
    set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g3 -D__STDC_FORMAT_MACROS -fdiagnostics-color=always")
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG -mtune=native -D__STDC_FORMAT_MACROS -fdiagnostics-color=always")
endif()
set(CMAKE_ASM_FLAGS "${CFLAGS} -x assembler-with-cpp")

# Generate compile_commands.json, to be read by clangd (editor diagnostics,
# code completion). Additionally, create a symlink to the compile_commands.json
# from the source directory since editors expect it to be in the source but
# cmake doesn't allow putting it there.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD_INCLUDE_DIRECTORIES ${CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES})
if(NOT ${CMAKE_BINARY_DIR} EQUAL ${PROJECT_SOURCE_DIR})
    file(RELATIVE_PATH relpath ${PROJECT_SOURCE_DIR} ${CMAKE_BINARY_DIR}/compile_commands.json)
    execute_process(COMMAND ln -sf ${relpath} ${PROJECT_SOURCE_DIR})
endif()

# Linting options
set(LINTING OFF)
option(INCLUDE_WHAT_YOU_USE "Enable include-what-you-use" OFF)
if (INCLUDE_WHAT_YOU_USE)
    find_program(iwyu include-what-you-use)
    if(NOT iwyu)
        message(FATAL_ERROR "No include-what-you-use found!")
    endif()
    if(NOT CLANG_BUILD)
        message(FATAL_ERROR "IWYU only works with the clang build")
    endif()
    set(LINTING ON)
    set(CMAKE_CXX_INCLUDE_WHAT_YOU_USE "${iwyu};-Xiwyu;--no_fwd_decls;-Xiwyu;--transitive_includes_only;-Xiwyu;--cxx17ns;-Xiwyu;--mapping_file=${PROJECT_SOURCE_DIR}/iwyu.mapping")
endif()

option(CLANG_TIDY "Enable clang-tidy" OFF)
if(CLANG_TIDY)
    find_program(clang_tidy clang-tidy)
    if(NOT clang_tidy)
        message(FATAL_ERROR "No clang-tidy found!")
    endif()
    if(NOT CLANG_BUILD)
        message(FATAL_ERROR "Clang-tidy only works with the clang build")
    endif()
    set(LINTING ON)
    set(CMAKE_CXX_CLANG_TIDY "${clang_tidy};--config-file=${PROJECT_SOURCE_DIR}/.clang-tidy-required;--header-filter=.;--warnings-as-errors=*")
endif()

if(LINTING)
    set(CMAKE_VERIFY_INTERFACE_HEADER_SETS ON)
endif()

# Use ccache to speed up builds if it is available
find_program(CCACHE_PROGRAM ccache)
set(compile_with_relative_paths_default OFF)
if(CCACHE_PROGRAM)
    set(compile_with_relative_paths_default ON)
    set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    set(CMAKE_CUDA_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
endif()

# Relative paths for improved ccache usage
option(COMPILE_WITH_RELATIVE_PATHS "Compile with relative paths for debug and definitions" ${compile_with_relative_paths_default})
if(COMPILE_WITH_RELATIVE_PATHS)
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fdebug-prefix-map=${PROJECT_SOURCE_DIR}=.")
endif()

option(PRECOMPILE_SOME_HEADERS "Pre-compile some (large) headers" ON)

include(CTest)
add_custom_target(all_unit_tests COMMAND make all install test)

# Attempt to automatically update submodules
if(GIT_FOUND AND EXISTS "${PROJECT_SOURCE_DIR}/.git")
    # Update submodules as needed
    option(GIT_SUBMODULE "Check submodules during build" ON)
    if(GIT_SUBMODULE)
        message(STATUS "Submodule update")
        execute_process(COMMAND ${GIT_EXECUTABLE} submodule update --init --recursive
                WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
                RESULT_VARIABLE GIT_SUBMOD_RESULT)
        if(NOT GIT_SUBMOD_RESULT EQUAL "0")
            message(FATAL_ERROR "${GIT_EXECUTABLE} submodule update --init --recursive failed with ${GIT_SUBMOD_RESULT}, please checkout submodules")
        endif()
    endif()
else()
    message(WARNING "Skipping submodule update (could not find one or both of a suitable git executable or this project's .git directory). This may result in other build failures.")
endif()

# Link repository infrastructure hooks into .git/hooks
if(IS_DIRECTORY "${PROJECT_SOURCE_DIR}/.git/hooks")
    message(STATUS "Copying hooks into .git/hooks")
    execute_process(COMMAND ln -sf ${PROJECT_SOURCE_DIR}/infrastructure/hooks/pre-commit ${PROJECT_SOURCE_DIR}/.git/hooks)
endif()

# Protobuf is used by event stream
find_package(Protobuf REQUIRED)

set(MINIMUM_PYTHON_VERSION 3.10)
# Older boost cmake versions prefer smaller versions first
set(BOOST_OPTIONAL_PYTHON_VERSIONS python310 python311 python312)

find_package(Boost 1.65.0 REQUIRED CONFIG
    COMPONENTS filesystem iostreams process program_options regex system
    OPTIONAL_COMPONENTS ${BOOST_OPTIONAL_PYTHON_VERSIONS} serialization)

list(REVERSE BOOST_OPTIONAL_PYTHON_VERSIONS)
foreach(python_version ${BOOST_OPTIONAL_PYTHON_VERSIONS})
    string(TOUPPER ${python_version} component_version)
    if(Boost_${python_version}_FOUND OR Boost_${component_version}_FOUND)
        if(python_version MATCHES "python3.+")
            string(REGEX REPLACE "python3(.+)" "3.\\1" version ${python_version})
            find_package(Python3 ${version} EXACT QUIET COMPONENTS Interpreter Development)
            if(NOT Python3_FOUND)
                continue()
            endif()
        else()
            find_package(Python3 ${MINIMUM_PYTHON_VERSION} QUIET COMPONENTS Interpreter Development)
        endif()
        set(BOOST_PYTHON_COMPONENT_NAME Boost::${python_version})
        break()
    endif()
endforeach()

if(NOT DEFINED BOOST_PYTHON_COMPONENT_NAME)
    message(FATAL_ERROR "Failed to find Boost python library")
endif()

if(Boost_SERIALIZATION_FOUND)
    add_compile_definitions(USE_BOOST_SERIALIZATION)
endif()

find_package(SystemCLanguage CONFIG REQUIRED)
add_definitions(-DSC_CPLUSPLUS=201103L)

find_library(KEYSTONE_LIB_PATH keystone HINTS LD_LIBRARY_PATH)

find_package(PkgConfig REQUIRED)
pkg_search_module(GLIB REQUIRED glib-2.0)

# Coverage build options
set(CMAKE_C_FLAGS_COVERAGE "--coverage")
set(CMAKE_CXX_FLAGS_COVERAGE "--coverage")
set(CMAKE_EXE_LINKER_FLAGS_COVERAGE "--coverage")

# ASAN build options
set(CMAKE_C_FLAGS_ASAN "${CMAKE_C_FLAGS_RELWITHDEBINFO} -fsanitize=address -fsanitize=undefined -fno-omit-frame-pointer -Wno-maybe-uninitialized")
set(CMAKE_CXX_FLAGS_ASAN "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} -fsanitize=address -fsanitize=undefined -fno-omit-frame-pointer -Wno-maybe-uninitialized")
set(CMAKE_EXE_LINKER_FLAGS_ASAN ${CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO} "-fsanitize=address -fsanitize=undefined")

# Release with debug messages
string(REPLACE "-DNDEBUG" "" debug_message_c_flags "${CMAKE_C_FLAGS_RELEASE}")
set(CMAKE_C_FLAGS_RELEASEWITHDEBMESSAGES "${debug_message_c_flags}")
string(REPLACE "-DNDEBUG" "" debug_message_cxx_flags "${CMAKE_CXX_FLAGS_RELEASE}")
set(CMAKE_CXX_FLAGS_CMAKE_C_FLAGS_RELEASEWITHDEBMESSAGES "${debug_message_cxx_flags}")

# Compiler-specific build options
if(CLANG_BUILD)
    add_compile_options(-Xclang -fno-pch-timestamp -Wno-unknown-warning-option -Wno-self-assign -Wno-deprecated-declarations)
else()
    add_compile_options(-fpch-preprocess -Wno-misleading-indentation)
endif()

option(CONCEPTS_DIAGNOSTICS "Enable C++ concepts diagnostics" OFF)
if(NOT CLANG_BUILD AND CONCEPTS_DIAGNOSTICS)
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-fconcepts-diagnostics-depth=2>)
endif()

option(REGRESS_TEST_KEEP_STUDY "Keep study output for regression test" OFF)

# Googletest package
find_package(GTest REQUIRED)
add_library(gtest ALIAS GTest::gtest)
add_library(gtest_main ALIAS GTest::gtest_main)
add_library(gmock ALIAS GTest::gmock)

# JSON package
find_package(nlohmann_json REQUIRED)
add_library(json ALIAS nlohmann_json::nlohmann_json)

# Immer package
find_package(Immer REQUIRED)
target_compile_options(immer INTERFACE -Wno-array-bounds)

# Capstone package
find_package(capstone QUIET)
if(NOT capstone_FOUND)
    pkg_check_modules(CAPSTONE REQUIRED IMPORTED_TARGET capstone)
    if(NOT CAPSTONE_FOUND)
        message(FATAL_ERROR "Unable to find capstone")
    endif()
    add_library(capstone ALIAS PkgConfig::CAPSTONE)
    add_library(capstone-static ALIAS PkgConfig::CAPSTONE)
else()
    add_library(capstone ALIAS capstone::capstone)
    add_library(capstone-static ALIAS capstone::capstone)
endif()

# Fmt package
find_package(fmt REQUIRED)
add_library(fmt ALIAS fmt::fmt-header-only)
add_library(fmt-header-only ALIAS fmt::fmt-header-only)
# Issues with gcc13 + fmt8.1.1
if(NOT CLANG_BUILD)
    target_compile_options(fmt::fmt-header-only INTERFACE -Wno-array-bounds -Wno-stringop-overflow)
endif()

# Internal include directories
include_directories(src)
include_directories(${CMAKE_CURRENT_BINARY_DIR}/src)
include_directories(${CMAKE_CURRENT_BINARY_DIR}/src/ampere/instruction_stream)

# This is required because we find the SystemC file using pkg-config
link_directories($<$<LINK_LANGUAGE:C,CXX>:${SYSTEMC_LIBRARY_DIRS}>)

add_subdirectory(src)
add_subdirectory(infrastructure)

if(USE_NIX)
    install(DIRECTORY nix DESTINATION .)
    install(FILES shell.nix DESTINATION .)
else()
    file(REMOVE_RECURSE ${CMAKE_INSTALL_PREFIX}/nix ${CMAKE_INSTALL_PREFIX}/shell.nix])
endif()

add_custom_target(
    CHECK_NIX ALL COMMAND ${CMAKE_COMMAND} -E env CMAKE_NIX_SHELL=${USE_NIX} bash ${CMAKE_CURRENT_SOURCE_DIR}/nix/check_nix_shell.sh
)
