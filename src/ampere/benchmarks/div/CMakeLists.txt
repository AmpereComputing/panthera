# Copyright (c) 2022-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

set(SOURCES
  dependent_advsimd_fdiv16
  dependent_advsimd_fdiv32
  dependent_advsimd_fdiv64
  dependent_gpr_udiv32
  dependent_gpr_udiv32_earlyexit
  dependent_gpr_udiv64
  dependent_gpr_udiv64_earlyexit
  dependent_scalar_fdiv16
  dependent_scalar_fdiv16_earlyexit
  dependent_scalar_fdiv32
  dependent_scalar_fdiv32_earlyexit
  dependent_scalar_fdiv64
  dependent_scalar_fdiv64_earlyexit
  independent_advsimd_fdiv16
  independent_advsimd_fdiv32
  independent_advsimd_fdiv64
  independent_gpr_udiv32
  independent_gpr_udiv32_earlyexit
  independent_gpr_udiv64
  independent_gpr_udiv64_earlyexit
  independent_scalar_fdiv16
  independent_scalar_fdiv16_earlyexit
  independent_scalar_fdiv32
  independent_scalar_fdiv32_earlyexit
  independent_scalar_fdiv64
  independent_scalar_fdiv64_earlyexit)

set(SVE_SOURCES
  dependent_sve_fdiv16
  dependent_sve_fdiv32
  dependent_sve_fdiv64
  dependent_sve_udiv32
  dependent_sve_udiv64
  independent_sve_fdiv16
  independent_sve_fdiv32
  independent_sve_fdiv64
  independent_sve_udiv32
  independent_sve_udiv64
  dependent_sve_fdiv16_earlyexit
  dependent_sve_fdiv32_earlyexit
  dependent_sve_fdiv64_earlyexit
  dependent_sve_udiv32_earlyexit
  dependent_sve_udiv64_earlyexit
  independent_sve_fdiv16_earlyexit
  independent_sve_fdiv32_earlyexit
  independent_sve_fdiv64_earlyexit
  independent_sve_udiv32_earlyexit
  independent_sve_udiv64_earlyexit)

add_compile_options(-march=armv8.2-a+fp16)

foreach(tst ${SOURCES})
  add_benchmark(${tst} AARCH_ONLY STANDALONE SOURCES main.c ${tst}.s)
  target_compile_options(${tst} PRIVATE -DTEST_FUNC=${tst})
endforeach()

add_compile_options(-march=armv8.5-a+fp16+sve)

foreach(tst ${SVE_SOURCES})
  add_benchmark(${tst} AARCH_ONLY STANDALONE CPU_FEATURE sve SOURCES main.c ${tst}.s)
  target_include_directories(${tst} PRIVATE
    ${PROJECT_SOURCE_DIR}/src/ampere/benchmarks/sve)
  target_compile_options(${tst} PRIVATE -DTEST_FUNC=${tst} -DUSE_SVE)
endforeach()
