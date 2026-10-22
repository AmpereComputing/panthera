# Copyright (c) 2023-2024 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

set(SOURCES
  smov
  fmov)

add_compile_options("-march=armv8.2-a+fp16")
foreach(tst ${SOURCES})
  add_benchmark(${tst} AARCH_ONLY STANDALONE SOURCES main.c ${tst}.s)
  target_compile_options(${tst} PRIVATE -DTEST_FUNC=${tst})
endforeach()

