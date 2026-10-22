# Copyright (c) 2024 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

# FIXME: issue with clang release-build with sim_sys_integration (segfault)
# (similar issue as panthera_python_test, when running event-tracer)
if(NOT CLANG_BUILD)
    add_panthera_test(sim_sys_integration
         SPECIFICATION ${CMAKE_CURRENT_SOURCE_DIR}/sim_integration.json
         WORKLOADS ${CMAKE_CURRENT_SOURCE_DIR}/dhrystone.json
         LABELS integration slow)

    set_property(TEST sim_sys_integration PROPERTY COST 322)
endif()
