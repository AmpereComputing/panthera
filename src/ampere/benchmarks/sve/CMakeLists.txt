# Copyright (c) 2022-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

add_compile_options("-march=armv8.6-a+fp16+sve2")
add_compile_options("-msve-vector-bits=128")

set(SOURCES
  svepred)

if(CMAKE_CXX_COMPILER_VERSION VERSION_GREATER 11.0.0)
  foreach(tst ${SOURCES})
    add_benchmark(${tst} AARCH_ONLY STANDALONE CPU_FEATURE sve SOURCES main.c ${tst}.s)
    target_compile_options(${tst} PRIVATE -DTEST_FUNC=${tst})
  endforeach()

  # add_benchmark(gather SOURCES gather.cc sve-exception.h)

  set(GATHER_TESTS
    gather2 gather4)

  foreach(tst ${GATHER_TESTS})
    add_benchmark(${tst} AARCH_ONLY STANDALONE CPU_FEATURE sve SOURCES main.c gather2.s)
    target_compile_options(${tst} PRIVATE -DTEST_FUNC=${tst})

    add_benchmark(${tst}-mem AARCH_ONLY STANDALONE CPU_FEATURE sve SOURCES main-gather-mem.c gather-mem.s)
    target_compile_options(${tst}-mem PRIVATE -DTEST_FUNC=${tst})

    add_benchmark(${tst}-mem-add AARCH_ONLY STANDALONE CPU_FEATURE sve SOURCES main-gather-mem.c gather-mem-add.s)
    target_compile_options(${tst}-mem-add PRIVATE -DTEST_FUNC=${tst})
  endforeach()

  # requires compiler that supports bfloat16....
  add_benchmark(bfdot AARCH_ONLY STANDALONE CPU_FEATURE sve SOURCES bfdot.cc procmaps.cc procmaps.h sve-exception.h)
  target_compile_options(bfdot PRIVATE -O3)

  add_benchmark(bfmmla AARCH_ONLY STANDALONE CPU_FEATURE sve SOURCES bfmmla.cc sve-exception.h)
  target_compile_options(bfmmla PRIVATE -O3)
endif()

add_benchmark(dblpmpi2f AARCH_ONLY STANDALONE CPU_FEATURE sve SOURCES dblpmpi2f.c)
target_compile_options(dblpmpi2f PRIVATE -O3 -march=armv8.5-a+sve)

add_benchmark(sve-pmu AARCH_ONLY STANDALONE CPU_FEATURE sve SOURCES sve-pmu.cc)
target_compile_options(sve-pmu PRIVATE -O0 -march=armv8.5-a+sve)
