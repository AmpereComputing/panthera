# Copyright (c) 2021-2026 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

cmake_minimum_required(VERSION 3.11)

add_executable(is_idiom_finder idiom_finder.cc)
target_link_libraries(is_idiom_finder instruction_stream capstone-static)

add_executable(is_validate is_validate.cc)
target_link_libraries(is_validate instruction_stream  archx  adis capstone-static sc_main_stub)

add_executable(is_data_reuse is_data_reuse.cc)
target_compile_options(is_data_reuse PRIVATE "-Wno-unused-but-set-variable")
target_link_libraries(is_data_reuse instruction_stream  archx sc_main_stub)

add_executable(is_translation_fixer is_translation_fixer.cc)
target_link_libraries(is_translation_fixer instruction_stream  archx sc_main_stub)

add_executable(is_footprint is_footprint.cc)
target_link_libraries(is_footprint instruction_stream fmt Boost::program_options)

add_executable(is_working_set is_working_set.cc)
target_link_libraries(is_working_set instruction_stream memory_stream fmt Boost::program_options)

add_executable(is_instr_reuse is_instr_reuse.cc)
target_link_libraries(is_instr_reuse instruction_stream Boost::program_options)

add_executable(is_loopfinder is_loopfinder.cc)
target_link_libraries(is_loopfinder ampere_sc  archx fmt sc_main_stub)

add_executable(is_branchtypes is_branchtypes.cc)
target_link_libraries(is_branchtypes instruction_stream archx sc_main_stub)

add_executable(is_count is_count.cc)
target_link_libraries(is_count archx  Boost::filesystem Boost::program_options fmt sc_main_stub)

add_executable(is_branch_csv is_branch_csv.cc)
target_link_libraries(is_branch_csv ampere_core ampere_sc uarch_common  archx  fmt Boost::program_options sc_main_stub)

add_executable(is_branch_hash is_branch_hash.cc)
target_link_libraries(is_branch_hash ampere_core ampere_sc uarch_common  archx fmt Boost::program_options sc_main_stub)
target_compile_options(is_branch_hash PRIVATE -Wno-unused-variable -Wno-maybe-uninitialized)

add_executable(is_mem_image is_mem_image.cc)
target_link_libraries(is_mem_image instruction_stream Boost::program_options fmt)

add_executable(is_barrier is_barrier.cc)
target_link_libraries(is_barrier  archx  Boost::filesystem Boost::program_options fmt sc_main_stub)

add_executable(is_uop_histo is_uop_histo.cc)
target_link_libraries(is_uop_histo   archx Boost::filesystem Boost::program_options fmt capstone-static sc_main_stub)

add_executable(is_arm_histo is_arm_histo.cc)
target_link_libraries(is_arm_histo adis capstone-static  archx Boost::filesystem fmt sc_main_stub)

add_executable(is_uop_cache is_uop_cache.cc)
target_link_libraries(is_uop_cache  archx Boost::filesystem fmt sc_main_stub)

add_executable(is_uop_print is_uop_print.cc)
target_link_libraries(is_uop_print instruction_stream  archx  Boost::filesystem fmt sc_main_stub)

add_executable(is_hints is_hints.cc)
target_link_libraries(is_hints instruction_stream  archx  fmt sc_main_stub)

add_executable(pv_soc_gen_csv pv_soc_gen_csv.cc)
target_link_libraries(pv_soc_gen_csv ampere_mesh ampere_sc archx fmt sc_main_stub)

add_executable(is_catus is_catus.cc)
target_link_libraries(is_catus  ampere_sc  archx fmt sc_main_stub)

install(TARGETS
    is_arm_histo
    is_barrier
    is_branch_csv
    is_branch_hash
    is_branchtypes
    is_catus
    is_count
    is_data_reuse
    is_footprint
    is_hints
    is_idiom_finder
    is_instr_reuse
    is_loopfinder
    is_mem_image
    is_translation_fixer
    is_uop_cache
    is_uop_histo
    is_uop_print
    is_validate
    is_working_set
    pv_soc_gen_csv
    DESTINATION bin)
