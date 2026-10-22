# Copyright (c) 2018-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

tab2enum(TAB early_exit.tab HFILE early_exit.enum.h CCFILE early_exit.enum.cc PYTHON early_exit.py)
tab2enum(TAB ixu.tab HFILE ixu.enum.h CCFILE ixu.enum.cc PYTHON ixu.py)

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/early_exit.py" DESTINATION lib/python/panthera/enums)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/ixu.py" DESTINATION lib/python/panthera/enums)

add_library(ampere_core_raw
    bp_bimodal.h
    bp_bimodal.cc
    bp_global.h
    bp_global.cc
    branch_check.h
    branch_check.cc
    branch_hash.h
    branch_hash.cc
    branch_history.h
    branch_history.cc
    branch_history_types.h
    branch_info.h
    branch_info.cc
    bpu.h
    bpu.cc
    bp_rsb.h
    bp_rsb.cc
    bp_tage.h
    bp_tage.cc
    bob_entry.h
    btb.cc
    btb.h
    coherency_validator.h
    core.cc
    core.h
    div_supp.h
    early_exit.enum.h
    early_exit.enum.cc
    fetch.h
    fetch.cc
    gather.h
    gpc.cc
    gpc.h
    icf.cc
    ifb.cc
    ifb_victim_cache.cc
    idr.cc
    instruction.h
    instruction.cc
    ixu_div_sqrt.cc
    ixu_dbg.h
    ixu_defs.h
    ixu.enum.h
    ixu.enum.cc
    ixu.h
    ixu.cc
    ixu_srt_div.h
    ixu_srt_div.cc
    backend_prefetcher.cc
    backend_prefetcher.h
    loop_predictor_table.cc
    perfect_fe.cc
    prediction.h
    prefetcher.h
    lsu.cc
    mmu.h
    mmu.cc
    nnp.h
    nnp.cc
    predictor.h
    uop.cc
    xu.h
    xu.cc
    loop_detector.cc
    loop_detector.h
    pwc.h)

target_link_libraries(ampere_core_raw PUBLIC ampere_sc archx ampere_memory ampere_mesh uarch_common memory_stream util json Boost::boost)
add_library(ampere_core INTERFACE)
target_link_libraries(ampere_core INTERFACE "$<LINK_LIBRARY:WHOLE_ARCHIVE,ampere_core_raw>")

add_subdirectory(testing)
add_subdirectory(test)

add_subdirectory(idr)
add_subdirectory(fsu)
add_subdirectory(lsu)
add_subdirectory(mmu)
add_subdirectory(l2)
add_subdirectory(prefetchers)
