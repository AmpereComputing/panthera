# Copyright (c) 2024-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

find_package(Protobuf REQUIRED)
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS aice_stream.proto)
protobuf_generate_python(PROTO_PY aice_stream.proto)
add_custom_target(aice_stream_proto_python ALL DEPENDS ${PROTO_PY})
install(FILES ${PROTO_PY} aice_trc.py aice_instruction_stream.py DESTINATION lib/python/panthera/streams)


set(GENERATOR_INPUTS
     ${CMAKE_CURRENT_SOURCE_DIR}/dtype.enum)


set(GENERATED_SOURCES dtype.cc)
set(GENERATED_HEADERS dtype.hh)
add_custom_command(
  OUTPUT ${GENERATED_SOURCES} ${GENERATED_HEADERS}
  DEPENDS enumer.py ${GENERATOR_INPUTS}
  COMMENT "Running the enumeration generator..."
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/enumer.py ${GENERATOR_INPUTS})

# Ignore array-bounds issues with generated protobuf code
set_source_files_properties(${PROTO_SRCS} PROPERTIES COMPILE_FLAGS -Wno-array-bounds)
set_source_files_properties(${PROTO_SRCS} ${PROTO_HDRS} PROPERTIES SKIP_LINTING ON)

set(SOURCES aice_instruction_stream.cc aice_instruction_stream.h)
add_library(aice_instruction_stream ${SOURCES} ${PROTO_SRCS} ${PROTO_HDRS})
set_property(TARGET aice_instruction_stream PROPERTY POSITION_INDEPENDENT_CODE ON)
target_include_directories(aice_instruction_stream PUBLIC ${PROTOBUF_INCLUDE_DIRS} ${Boost_INCLUDE_DIRS})
target_link_libraries(aice_instruction_stream PUBLIC protobuf_utils Boost::filesystem INTERFACE ${PROTOBUF_LIBRARIES})



add_library(kestrel_aice_raw
      aice_sim.h
      aice_sim.cc
      aice_instruction.h
      aice_instruction.cc
      aice_test_instr.h
      aice_mem_req.h
      aice_tmem_fsm.h
      aice_common.h
      aice_tmem.h
      aice_ramp.h
      aice_ramp.cc
      ${GENERATED_HEADERS}
      ${GENERATED_SOURCES}
      ${PROTO_SRCS}
      ${PROTO_HDRS})
target_compile_options(kestrel_aice_raw     PRIVATE "-Wextra" "-Wno-unused")
target_include_directories(kestrel_aice_raw PUBLIC  ${PROTOBUF_INCLUDE_DIRS} ${Boost_INCLUDE_DIRS})
target_link_libraries(kestrel_aice_raw      PUBLIC  aice_instruction_stream ampere_sc
                                                   protobuf_utils Boost::filesystem
                                                   INTERFACE ${PROTOBUF_LIBRARIES})

add_library(kestrel_aice INTERFACE)
target_link_libraries(kestrel_aice INTERFACE "$<LINK_LIBRARY:WHOLE_ARCHIVE,kestrel_aice_raw>")

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/version.h.in" "${CMAKE_CURRENT_BINARY_DIR}/version.h" @ONLY)
add_executable(aice-sim
                  aice_sim_main.cc
                  ${CMAKE_CURRENT_BINARY_DIR}/version.h)
target_include_directories(aice-sim PUBLIC  kestrel_aice)
target_link_libraries(aice-sim PUBLIC  kestrel_aice)
install(TARGETS aice-sim)

add_executable(aice_viewer aice_instruction_stream_viewer.cc
                           ${GENERATED_HEADERS}
                           ${GENERATED_SOURCES}
                           ${PROTO_SRCS} ${PROTO_HDRS})
target_link_libraries(aice_viewer aice_instruction_stream)
install(TARGETS aice_viewer)


install(FILES aice-events.cfg DESTINATION config/events.tracing)
add_subdirectory(test)
