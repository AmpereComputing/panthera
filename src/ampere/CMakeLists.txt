# Copyright (c) 2023-2026 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

add_compile_options(-Wall -Werror)

include(cmake/benchmarks.cmake)
include(cmake/tables.cmake)
include(cmake/tests.cmake)

add_subdirectory(archx)
add_subdirectory(adis)
set_source_files_properties(DIRECTORY adis PROPERTIES SKIP_LINTING ON)

if(NOT X86 AND NOT CMAKE_BUILD_TYPE_NORMALIZED STREQUAL "ASAN")
    add_subdirectory(benchmarks)
endif()

add_subdirectory(bobcat)
add_subdirectory(branch_stream)
add_subdirectory(core)
add_subdirectory(config/test)
add_subdirectory(dectab)
add_subdirectory(event_stream)
add_subdirectory(evp)
add_subdirectory(example)
add_subdirectory(instruction_stream)
add_subdirectory(kestrel)
add_subdirectory(lynx)
add_subdirectory(memory)
add_subdirectory(memory_stream)
add_subdirectory(mesh)
add_subdirectory(protobuf_utils)
add_subdirectory(python)
add_subdirectory(sc)
add_subdirectory(soc)
add_subdirectory(sys)
add_subdirectory(tigris)
add_subdirectory(tools)
add_subdirectory(tracing_tools)
add_subdirectory(uarch_common)
add_subdirectory(util)

install(PROGRAMS panthera DESTINATION bin)
install(DIRECTORY config DESTINATION . PATTERN "test" EXCLUDE PATTERN "test/*" EXCLUDE)
