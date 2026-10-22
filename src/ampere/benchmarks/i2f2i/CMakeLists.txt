# Copyright (c) 2024 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

set(SOURCES i2f2i)

foreach(tst ${SOURCES})
  add_benchmark(${tst} AARCH_ONLY STANDALONE SOURCES main.c ${tst}.s)
  target_compile_options(${tst} PRIVATE -DTEST_FUNC=${tst})
endforeach()

