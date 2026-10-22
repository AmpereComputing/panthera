# Copyright (c) 2024 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

set(TESTS
    test_addr.cc
    test_driver.cc
    test_driver_bs.cc
    test_instruction.cc
    test_decode.cc
    test_uop_info.cc
    test_arch_state.cc
    test_xlat_decode.cc
    test_prefetch_address.cc
    test_sysreg.cc)

add_cpp_test(archx_unit SOURCES ${TESTS})
target_link_libraries(archx_unit archx sc_main_stub)
target_compile_options(archx_unit PRIVATE -Wno-sign-compare)
