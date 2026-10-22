# Copyright (c) 2024-2026 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

# FIXME: issue with clang release-build with panthera_python_test (segfault)
if(NOT CLANG_BUILD)
    set(python_test_directories
        catscan
        cheetah
        events
        report
        rollup
        snippets
        tools
        tracing)

    foreach(name ${python_test_directories})
        add_python_test(panthera_${name}_python_test DISCOVER ${CMAKE_CURRENT_SOURCE_DIR}/${name})
    endforeach()

    file(GLOB top_level_tests ${CMAKE_CURRENT_SOURCE_DIR}/test_*.py)
    add_python_test(panthera_python_test TESTS ${top_level_tests})
    set_property(TEST panthera_cheetah_python_test PROPERTY COST 120)
endif()
