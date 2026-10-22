# Copyright (c) 2023-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

set(TESTS
    test_bpu.cc
    test_icf.cc
    test_ifb.cc
    test_perfect_fe.cc
    test_idr.cc
    test_idr_load_balancing.cc
    test_idr_counter_balancing.cc
    test_hash.cc
    test_l2c.cc
    test_lsu.cc
    test_mmu.cc
    test_tlb.cc
    test_l1tlb.cc
    test_l2c_pwc.cc
    test_ixu.cc
    test_uop.cc
    test_xu.cc
    test_gpc.cc
    test_bp_bimodal.cc
    test_bp_global.cc
    test_branch_context.cc
    test_branch_history.cc
    test_return_stack.cc
    test_btb.cc
    test_bp_tage.cc
    test_loop_detector.cc
    test_l2crp_nru.cc
    test_l2crp_rrip.cc
    test_l2crp_brrip.cc
    test_l2crp_drrip.cc)

add_cpp_test(core_unit SYSTEMC SOURCES ${TESTS})
target_link_libraries(core_unit ampere_core ampere_mesh core_testing ampere_soc uarch_common ampere_sc_testing ${KEYSTONE_LIB_PATH})
target_compile_options(core_unit PRIVATE -Wno-sign-compare)

add_cpp_test(core_double_pump_i2f SYSTEMC SOURCES test_double_pump.cc)
target_link_libraries(core_double_pump_i2f uarch_common ampere_core ampere_mesh ampere_memory archx ampere_soc ampere_sc_testing)
target_compile_options(core_double_pump_i2f PRIVATE -Wno-sign-compare)

add_cpp_test(core_emulation SYSTEMC SOURCES test_emulation.cc)
target_link_libraries(core_emulation uarch_common ampere_core ampere_mesh ampere_memory archx ampere_soc ampere_sc_testing)
target_compile_options(core_emulation PRIVATE -Wno-sign-compare)

set(core_integration_tests
    other_configs
    load_latency
    ops
    sys
    div
    l1bop
    gretch
    writeback_l1d
    lsu_l2_prefetch
    mispredict
    fsu_uvw_pipes)

foreach(name ${core_integration_tests})
     add_cpp_test(core_integration_${name} SYSTEMC SOURCES test_core_${name}.cc LABELS integration slow)
     target_link_libraries(core_integration_${name} uarch_common ampere_core ampere_mesh ampere_soc ampere_memory archx ampere_sc_testing)
     target_compile_options(core_integration_${name} PRIVATE -Wno-sign-compare)
     set_property(TEST core_integration_${name} PROPERTY COST 30)
endforeach()

add_cpp_test(core_bpu SYSTEMC SOURCES test_core_bpu.cc LABELS integration slow)
target_link_libraries(core_bpu uarch_common ampere_core ampere_mesh ampere_memory archx ampere_soc ampere_sc_testing)
target_compile_options(core_bpu PRIVATE -Wno-sign-compare)
set_property(TEST core_bpu PROPERTY COST 16)

if(REGRESS_TEST_KEEP_STUDY)
     add_panthera_test(core_fast_regress KEEP
          SPECIFICATION ${CMAKE_CURRENT_SOURCE_DIR}/fast_regress_configs.json
          WORKLOADS ${CMAKE_CURRENT_SOURCE_DIR}/fast_regress_workloads.json
          LABELS regression slow)
else()
     add_panthera_test(core_fast_regress
          SPECIFICATION ${CMAKE_CURRENT_SOURCE_DIR}/fast_regress_configs.json
          WORKLOADS ${CMAKE_CURRENT_SOURCE_DIR}/fast_regress_workloads.json
          LABELS regression slow)
endif()
set_property(TEST core_fast_regress PROPERTY COST 302)

add_panthera_test(core_long_regress OPTIONAL KEEP PARALLEL
     SPECIFICATION ${CMAKE_CURRENT_SOURCE_DIR}/long_regress_configs.json
     WORKLOADS ${CMAKE_CURRENT_SOURCE_DIR}/fast_regress_workloads.json)

add_cpp_test(test_srt_div SOURCES test_srt_div.cc ${CMAKE_CURRENT_SOURCE_DIR}/../ixu_srt_div.cc)
target_include_directories(test_srt_div PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(test_srt_div gtest_main fmt Boost::boost)
