# Copyright (c) 2024-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

add_benchmark(bm_memcpy SOURCES test_memcpy.cc)
target_compile_options(bm_memcpy PRIVATE -O3)

# FIXME: unable to get non-fp/neon version of memcpy, may need to use an older/different compiler
#if(${CMAKE_SYSTEM_PROCESSOR} STREQUAL "aarch64")
#    add_benchmark(bm_memcpy_nofp SOURCES test_memcpy.cc)
#    target_compile_options(bm_memcpy_nofp PRIVATE -O3 -ffreestanding -nostdlib -march=armv8-a+nofp -mgeneral-regs-only)
#endif()
