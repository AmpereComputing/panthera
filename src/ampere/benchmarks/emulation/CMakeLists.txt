# Copyright (c) 2024 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

set(SOURCES
  emu_1)

add_compile_options("-march=armv8.5-a+fp16+sve")

foreach(tst ${SOURCES})
  add_benchmark(${tst} AARCH_ONLY STANDALONE CPU_FEATURE sve SOURCES main.c ${tst}.s)
  target_include_directories(${tst} PRIVATE ${PROJECT_SOURCE_DIR}/src/ampere/benchmarks/sve)
  target_compile_options(${tst} PRIVATE -DTEST_FUNC=${tst})
endforeach()
