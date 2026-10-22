# Copyright (c) 2019-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

# Some micros use volatile which was heavily limited in c++20
set_property(DIRECTORY PROPERTY CXX_STANDARD 17)

add_compile_options(-Wall -Werror)
add_library(benchmark STATIC benchmark.cc benchmark.h main.cc)
target_link_libraries(benchmark PUBLIC gtest fmt)

add_benchmark(bm_vector SOURCES test_vector.cc vector_tmpl.h)
target_compile_options(bm_vector PRIVATE
        -ftree-vectorize -funroll-loops -funsafe-math-optimizations
        -falign-functions=32 -falign-loops=32)

add_benchmark(bm_scalar SOURCES test_vector.cc vector_tmpl.h)
target_compile_options(bm_scalar PRIVATE -O2 -funroll-loops)

add_benchmark(bm_mem_lat SOURCES test_mem_lat.cc ARGUMENTS --gtest_filter=-:*Random*/4:*Random*/5:*Random*/6:*Random*/7:*Random*/8:*Random*/9:*Random*/10:*Random*/11:*Random*/12:*Random*/13:*Random*/14:*Random*/15:*Random*/16:*Random*/17)
target_compile_options(bm_mem_lat PRIVATE -Ofast)

add_benchmark(bm_mem_bw SOURCES test_mem_bw.cc)
target_compile_options(bm_mem_bw PRIVATE -Ofast)

add_benchmark(bm_branch SOURCES test_branch.cc)
target_compile_options(bm_branch PRIVATE -Ofast)

add_benchmark(bm_mem_lat_vec AARCH_ONLY SOURCES test_mem_lat_vec.cc ARGUMENTS --gtest_filter=-:*Random*/4:*Random*/5:*Random*/6:*Random*/7:*Random*/8:*Random*/9:*Random*/10:*Random*/11:*Random*/12:*Random*/13:*Random*/14:*Random*/15:*Random*/16:*Random*/17)
target_compile_options(bm_mem_lat_vec PRIVATE -Ofast)

set(alu_sources
    independent_fadds.s
    independent_fmuls.s
    dependent_fadds.s
    dependent_fmuls.s
    isb.s
    independent_fdivs.s
    independent_idivs.s
    test_alu.cc
)
add_benchmark(bm_alu AARCH_ONLY SOURCES ${alu_sources})
target_compile_options(bm_alu PRIVATE -Ofast)

set(alu_mem_sources
    independent_loads.s
    independent_stores.s
    independent_adds_loads.s
    load_store.s
    independent_fadds_stores.s
    load_store_fsu.s
    test_alu_mem.cc
)
add_benchmark(bm_alu_mem AARCH_ONLY SOURCES ${alu_mem_sources})
target_compile_options(bm_alu_mem PRIVATE -O2)

set(alu_fusion_sources
    mov_movk_fusion.s
    mov_movk_movk_fusion.s
    load_store.s
    test_alu_fusion.cc
)
add_benchmark(bm_alu_fusion AARCH_ONLY SOURCES ${alu_fusion_sources})
target_compile_options(bm_alu_fusion PRIVATE -O2 -g)

add_benchmark(bm_loop AARCH_ONLY SOURCES double_branch_loop.s test_loop.cc)
target_compile_options(bm_loop PRIVATE -O2 -g)

add_benchmark(bm_bandwidth AARCH_ONLY SOURCES load_bandwidth.s test_bandwidth.cc)
target_compile_options(bm_bandwidth PRIVATE -O2)

add_benchmark(bm_rapid_call_ret AARCH_ONLY SOURCES rapid_call_ret.s test_rapid_call_ret.cc)
target_compile_options(bm_rapid_call_ret PRIVATE -O2)

add_benchmark(bm_branch_pmus AARCH_ONLY SOURCES branch_pmus.s test_branch_pmus.cc)
target_compile_options(bm_branch_pmus PRIVATE -O2)

set(fp_mov_lat_sources
    test_fmov_lat.cc
    fp_mov_lat/fpconvert-lat-2stream.S
    fp_mov_lat/fpconvert-lat-2stream.S
    fp_mov_lat/fpconvert-lat-4stream.S
    fp_mov_lat/fpconvert-lat-t.S
    fp_mov_lat/fpconvert-lat-w.S
    fp_mov_lat/fpconvert-lat.S
    fp_mov_lat/fpmov-lat-2stream.S
    fp_mov_lat/fpmov-lat-4stream.S
    fp_mov_lat/fpmov-lat-t.S
    fp_mov_lat/fpmov-lat-w.S
    fp_mov_lat/fpmov-lat.S
    fp_mov_lat/fpmov-lat-spaced.S
    fp_mov_lat/fpconvert-lat-spaced.S
)
add_benchmark(bm_fmov_lat AARCH_ONLY SOURCES ${fp_mov_lat_sources})
target_compile_options(bm_fmov_lat PRIVATE -O2)

add_benchmark(bm_stld_lat AARCH_ONLY SOURCES test_stld_lat.cc stld-lat.S)
target_compile_options(bm_stld_lat PRIVATE -O2)

add_benchmark(bm_l2tlb AARCH_ONLY SOURCES test_l2tlb.cc l2tlb/l2tlb-data-lat.S l2tlb/l2tlb-data-bw.S)
target_compile_options(bm_l2tlb PRIVATE -O2)

add_benchmark(bm_l1_thrash AARCH_ONLY SOURCES test_l1_thrash.cc l1-thrash/l1-thrash-lat.S l1-thrash/l1-thrash-bw.S)
target_compile_options(bm_l1_thrash PRIVATE -O2)

add_benchmark(bm_splits AARCH_ONLY SOURCES test_splits.cc splits/load-split-lat.S splits/load-split-bw.S)
target_compile_options(bm_splits PRIVATE -O2)

add_benchmark(bm_cleaving AARCH_ONLY SOURCES test_cleaving.cc cleaving/cleaving_int.S)
target_compile_options(bm_cleaving PRIVATE -O2)

add_benchmark(bm_streaming_mem AARCH_ONLY SOURCES test_streaming_mem.cc streaming_mem.S)
target_compile_options(bm_streaming_mem PRIVATE -O2)

# This test is an infinite loop -- mark it SLOW so it doesn't get run as a test
add_benchmark(bm_mispredict AARCH_ONLY SLOW SOURCES test_mispredict.cc mispredict.s)
target_compile_options(bm_mispredict PRIVATE -O2)

add_benchmark(bm_slow_integer AARCH_ONLY SOURCES test_slow_integer.cc slow_integer.S)
target_compile_options(bm_slow_integer PRIVATE -O2)

add_benchmark(bm_load_cancel AARCH_ONLY SOURCES test_load_cancel.cc load_cancel.S)
target_compile_options(bm_load_cancel PRIVATE -O2)

set(fmla_bw_sources
    test_fmla_chain.cc
    fmla_chain/fmla-chain-par-2.S
    fmla_chain/fmla-chain-par-4.S
    fmla_chain/fmla-chain-par-6.S
    fmla_chain/fmla-chain-par-8.S
    fmla_chain/fmla-chain.S
)
add_benchmark(bm_fmla_bw AARCH_ONLY SOURCES ${fmla_bw_sources})
target_compile_options(bm_fmla_bw PRIVATE -O2)

add_benchmark(bm_tbnz_alignment AARCH_ONLY SOURCES test_tbnz_alignment.cc tbnz_alignment/tbnz_alignment_wrapper.S)
target_compile_options(bm_tbnz_alignment PRIVATE -O2)

add_custom_command(OUTPUT code_streaming/code_streaming.S COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/code_streaming/gen_code_streaming.py WORKING_DIRECTORY code_streaming)
add_benchmark(bm_code_streaming AARCH_ONLY SOURCES test_code_streaming.cc code_streaming/code_streaming.S)
target_compile_options(bm_code_streaming PRIVATE -O2)

add_benchmark(store-hit-dmb AARCH_ONLY STANDALONE SOURCES store-hit-dmb.c)

add_benchmark(store-miss-dmb AARCH_ONLY STANDALONE SOURCES store-miss-dmb.c)

add_benchmark(casl-hit AARCH_ONLY STANDALONE SOURCES casl-hit.c)
target_compile_options(casl-hit PRIVATE -O3 -march=armv8.5-a)

add_benchmark(casl-miss AARCH_ONLY STANDALONE SOURCES casl-miss.c)
target_compile_options(casl-miss PRIVATE -O3 -march=armv8.5-a)

add_benchmark(ldar-stlr AARCH_ONLY STANDALONE SOURCES ldar-stlr.c)
target_compile_options(ldar-stlr PRIVATE -O3)

add_benchmark(bm_call SOURCES test_call.cc)
target_compile_options(bm_call PRIVATE -Ofast)

add_benchmark(l2miss STANDALONE SOURCES l2miss.c)

add_benchmark(sequential-stream STANDALONE SOURCES sequential-stream.c)

add_subdirectory(fsu-mov)
add_subdirectory(sve)
add_subdirectory(div)
add_subdirectory(emulation)
add_subdirectory(i2f2i)
add_subdirectory(prfm)
add_subdirectory(atomics)
add_subdirectory(memcpy)
add_subdirectory(patterns)
