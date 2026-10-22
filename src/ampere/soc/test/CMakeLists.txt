# Copyright (c) 2023-2026 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

add_compile_options(-Wno-sign-compare)
link_libraries(ampere_soc archx uarch_common ampere_core ampere_mesh ampere_memory ampere_sc_testing)

set(single_soc_tests
    bandwidth
    hemi_config
    hemi_config_bw
    kestrel_slice
    l3_warm
    pha
    quad_config
    read_latency
    slc
    slc_stash
    stress_bandwidth
    stress_iortest
    stress_prog0r1w
    stress_prog1r1w
    stress_prog1r2w
    stress_prog1r3w
    stress_prog1r4w
    stress_prog1r5w
    stress_prog1r6w
    stress_prog2r1w
    stress_prog3r1w
    stress_prog4r1w
    stress_prog5r1w
    stress_prog6r1w)

foreach(name ${single_soc_tests})
    add_cpp_test(soc_integration_${name} SYSTEMC SOURCES test_${name}.cc LABELS integration slow)
    set_property(TEST soc_integration_${name} PROPERTY COST 15)
endforeach()
