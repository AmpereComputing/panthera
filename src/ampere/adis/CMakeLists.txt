#
# Copyright (c) 2022 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
#

cmake_minimum_required(VERSION 3.14)
project(adis)
find_package(Python3 COMPONENTS Interpreter Development REQUIRED)
find_package(fmt REQUIRED)
set(CMAKE_CXX_STANDARD 20)
option(BUILD_SHARED_LIBS "Build using shared libraries" OFF)

set (WARNING_FLAGS "")
if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  set (WARNING_FLAGS "-Wall -Wextra -Werror -pedantic -Wshadow=compatible-local")
elseif (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  set (WARNING_FLAGS "-Wall -Wextra -Werror -pedantic")
endif()
set (DEBUG_FLAGS "")

set(AARCHMRS_DIR "${PROJECT_SOURCE_DIR}/data/aarchmrs-2026-09")
set(AARCHMRS_INSTRUCTIONS "${AARCHMRS_DIR}/Instructions.json.gz")
set(AARCHMRS_IMPLEMENTATION_POLICY "${AARCHMRS_DIR}/implemented-encodings.json")

set (CMAKE_CXX_FLAGS_DEBUG    "${CMAKE_CXX_FLAGS_DEBUG}    ${WARNING_FLAGS} ${DEBUG_FLAGS}")
set (CMAKE_LINKER_FLAGS_DEBUG "${CMAKE_LINKER_FLAGS_DEBUG} ${WARNING_FLAGS} ${DEBUG_FLAGS}")

# use -DCMAKE_BUILD_TYPE=ASAN
set(CMAKE_C_FLAGS_ASAN "-fsanitize=address -fsanitize=undefined -fno-omit-frame-pointer")
set(CMAKE_CXX_FLAGS_ASAN "-fsanitize=address -fsanitize=undefined -fno-omit-frame-pointer")
set(CMAKE_EXE_LINKER_FLAGS_ASAN "-fsanitize=address -fsanitize=undefined")

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${WARNING_FLAGS}")
set(CMAKE_C_FLAGS   "${CMAKE_C_FLAGS}   ${WARNING_FLAGS}")

file(GLOB PYFILES "${PROJECT_SOURCE_DIR}/pysrc/*py")

set(STATIC_ADIS_SOURCES
     ${PROJECT_SOURCE_DIR}/src/adis.cc
     ${PROJECT_SOURCE_DIR}/src/adis-vdat.cc
     ${PROJECT_SOURCE_DIR}/src/adis-disasm.cc
     ${PROJECT_SOURCE_DIR}/src/adis-operand-iter.cc
     ${PROJECT_SOURCE_DIR}/src/adis-sysreg.cc
     ${PROJECT_SOURCE_DIR}/src/adis-opnd-helper.cc
     ${PROJECT_SOURCE_DIR}/src/adis-branch-target.cc
     ${PROJECT_SOURCE_DIR}/src/adis-dec-imm13.cc)
set(STATIC_ADIS_HEADERS
     ${PROJECT_SOURCE_DIR}/src/adis-vdat.h
     ${PROJECT_SOURCE_DIR}/src/adis-intfc.h
     ${PROJECT_SOURCE_DIR}/src/adis-branch-target.h
     ${PROJECT_SOURCE_DIR}/src/adis-opnd-helper.h
     ${PROJECT_SOURCE_DIR}/src/adis-properties.h
     ${PROJECT_SOURCE_DIR}/src/adis-dec-imm13.h
     ${PROJECT_SOURCE_DIR}/src/adis-asm-pattern.h
     ${PROJECT_SOURCE_DIR}/src/adis-signext.h
     ${PROJECT_SOURCE_DIR}/src/adis-operand-iter.h
     ${PROJECT_SOURCE_DIR}/src/adis-sysreg.h
     ${PROJECT_SOURCE_DIR}/src/adis-sysreg-map.h
     ${PROJECT_SOURCE_DIR}/src/adis.h)
set(GENERATED_ADIS_SOURCES
     ${PROJECT_BINARY_DIR}/dectab.cc
     ${PROJECT_BINARY_DIR}/adis-static-iinfo.cc
     ${PROJECT_BINARY_DIR}/adis-tables.cc
     ${PROJECT_BINARY_DIR}/adis-strtab-enum.cc
     #${PROJECT_BINARY_DIR}/adis-field-disas.cc
     ${PROJECT_BINARY_DIR}/adis-encoding-fields.cc
     ${PROJECT_BINARY_DIR}/adis-decodeable-items.cc
     ${PROJECT_BINARY_DIR}/adis-asm-pattern.cc
     ${PROJECT_BINARY_DIR}/adis-encoding-enum.cc)
set(GENERATED_ADIS_HEADERS
     ${PROJECT_BINARY_DIR}/dectab.h
     ${PROJECT_BINARY_DIR}/adis-field-extract.h
     ${PROJECT_BINARY_DIR}/adis-tables.h
     ${PROJECT_BINARY_DIR}/adis-sys-alias-qualifiers.h
     ${PROJECT_BINARY_DIR}/adis-static-iinfo.h
     ${PROJECT_BINARY_DIR}/adis-strtab-enum.h
     ${PROJECT_BINARY_DIR}/adis-encoding-fields.h
     #${PROJECT_BINARY_DIR}/adis-field-disas.h
     ${PROJECT_BINARY_DIR}/adis-decodeable-items.h
     ${PROJECT_BINARY_DIR}/adis-encoding-enum.h
     ${PROJECT_BINARY_DIR}/adis-const.h)
set(HEADERS
     ${STATIC_ADIS_HEADERS}
     ${GENERATED_ADIS_HEADERS})

add_custom_command(
  OUTPUT ${GENERATED_ADIS_SOURCES} ${GENERATED_ADIS_HEADERS}
  DEPENDS ${PYFILES} ${AARCHMRS_INSTRUCTIONS} ${AARCHMRS_IMPLEMENTATION_POLICY}
  COMMENT "Running the generator..."
  COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/pysrc/genadis.py
          --aarchmrs ${AARCHMRS_INSTRUCTIONS} > adis-gen-log.txt)

include_directories(src ${PROJECT_BINARY_DIR})
# not setting static or shared. controlled by BUILD_SHARED_LIBS option above
add_library(adis ${STATIC_ADIS_SOURCES} ${GENERATED_ADIS_SOURCES})
# use PIC even when not building shared so that shared libs can link to it
set_property(TARGET adis PROPERTY POSITION_INDEPENDENT_CODE ON)
set_property(TARGET adis PROPERTY INSTALL_RPATH_USE_LINK_PATH TRUE)
set_property(TARGET adis PROPERTY PUBLIC_HEADER "${HEADERS}")
target_include_directories(adis PUBLIC ${PROJECT_BINARY_DIR} ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(adis PUBLIC fmt::fmt-header-only)

#add_library(adis-shared SHARED ${STATIC_ADIS_SOURCES} ${GENERATED_ADIS_SOURCES})
#set_property(TARGET adis-shared PROPERTY PUBLIC_HEADER "${HEADERS}")
#target_include_directories(adis-shared PUBLIC ${PROJECT_BINARY_DIR} ${PROJECT_SOURCE_DIR}/src)

add_executable(tadis src/main.cc)
target_link_libraries(tadis adis)

add_executable(testadis src/tester.cc)
target_link_libraries(testadis adis fmt::fmt-header-only)

add_executable(adis-disasm-unit src/test-disasm.cc)
target_link_libraries(adis-disasm-unit adis)

add_executable(disraw src/disraw.cc)
target_link_libraries(disraw adis)

add_executable(dismacho src/dismacho.cc src/disargs.cc src/dis-common.cc)
target_link_libraries(dismacho adis)

add_executable(diself src/diself.cc src/disargs.cc src/dis-common.cc)
target_link_libraries(diself adis)

# test program for decoding logical imm13 (n:imms:immr) immediates
add_executable(test-dec-imm13 src/test-dec-imm13.cc)
target_link_libraries(test-dec-imm13 adis)
target_link_libraries(test-dec-imm13 fmt::fmt-header-only)

add_test(NAME adis_aarchmrs
         COMMAND ${Python3_EXECUTABLE} -m unittest -v test_aarchmrs.py)
set_tests_properties(adis_aarchmrs PROPERTIES
                     WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}/pysrc
                     LABELS unit)

add_test(NAME adis_disasm_unit COMMAND adis-disasm-unit)
set_tests_properties(adis_disasm_unit PROPERTIES LABELS unit)

foreach(exe tadis disraw dismacho diself)
  set_property(TARGET ${exe} PROPERTY INSTALL_RPATH_USE_LINK_PATH TRUE)
  install(TARGETS ${exe} DESTINATION bin)
endforeach()

if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
  set(CMAKE_INSTALL_PREFIX "${CMAKE_SOURCE_DIR}/install" CACHE PATH "default install path" FORCE )
endif()


install(TARGETS adis
        ARCHIVE DESTINATION lib
        PUBLIC_HEADER DESTINATION include/adis)
#install(TARGETS adis-shared
#        ARCHIVE DESTINATION lib
#        PUBLIC_HEADER DESTINATION include/adis)
#install(FILES ${HEADERS} DESTINATION include/adis)

# build the python extension
set(pyext_MODULE_NAME "adis" CACHE STRING "Name of the C++ extension module file")
add_subdirectory(pyext)
