# Copyright (c) 2023-2026 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

# Model config tests
add_library(config_test config_test.h)
target_link_libraries(config_test uarch_common ampere_core ampere_mesh ampere_soc ampere_memory archx test_sc_main ampere_sc_testing)

add_cpp_test(sys_config_integration SOURCES test_sys_configs.cc LABELS integration config)
target_link_libraries(sys_config_integration config_test)

add_cpp_test(core_config_integration SOURCES test_core_configs.cc LABELS integration config)
target_link_libraries(core_config_integration config_test)

# Cheetah JSON specification checks
file(GLOB config_files "${CMAKE_CURRENT_SOURCE_DIR}/../cheetah/json/*.json")
add_test(NAME cheetah_json_config_specifications
    COMMAND check-jsonschema
    --schemafile ${PROJECT_SOURCE_DIR}/src/ampere/python/panthera/cheetah/schema/specification.json
    ${config_files}
)

option(CHEETAH_DRYRUN_TESTS "Enable running cheetah config dryrun tests" ON)
if(CHEETAH_DRYRUN_TESTS)
    # Cheetah python config tests
    set(TRACELIST ${CMAKE_CURRENT_SOURCE_DIR}/testing/cheetah_workloads.json)
    set(CORE_PYTHON_CONFIGS
        core_study
        flakiness_study
        rtl_snippets_study
        is_loopfinder_study
    )
    set(PYTHON_CONFIGS
        domino-study
    )

    # Config-specific args
    set(domino_study_args --latency --skip-validation)

    foreach(config ${CORE_PYTHON_CONFIGS})
        string(REPLACE - _ name ${config})
        set(config_args --debug)
        if(config MATCHES ".*rtl_snippets.*")
            list(APPEND config_args --no-l2-warmup)
        endif()
        add_panthera_test(cheetah_python_${name}_dryrun DRYRUN FILELESS VALIDATE PYTHON
            SPECIFICATION ${CMAKE_INSTALL_PREFIX}/config/cheetah/python/${config}.py
            ARGUMENTS ${config_args}
            WORKLOADS ${TRACELIST}
            LABELS integration cheetah_python config veryslow)
        set_property(TEST cheetah_python_${name}_dryrun PROPERTY COST 200)
    endforeach()
    foreach(config ${PYTHON_CONFIGS})
        string(REPLACE - _ name ${config})
        set(config_args --debug)
        if(DEFINED ${name}_args)
            list(APPEND config_args ${${name}_args})
        endif()
        add_panthera_test(cheetah_python_${name}_dryrun DRYRUN FILELESS VALIDATE PYTHON
            SPECIFICATION ${CMAKE_INSTALL_PREFIX}/config/cheetah/python/${config}.py
            ARGUMENTS ${config_args}
            LABELS integration cheetah_python config veryslow)
    endforeach()

    add_panthera_test(cheetah_python_command_study_dryrun DRYRUN FILELESS PYTHON
            SPECIFICATION ${CMAKE_INSTALL_PREFIX}/config/cheetah/python/command_study.py
            ARGUMENTS "{panthera_root}/bin/is_info {workload_root}/{file} > {name}.out"
            WORKLOADS ${TRACELIST}
            LABELS integration cheetah_python config veryslow)

    # Cheetah JSON config tests
    set(JSON_CONFIGS
        # Studies
        siryn_study
        banshee_study
        rogue_study
        domino_study
        nebula_study
        # Snippets
        rtl_snippets_domino
    )

    foreach(config ${JSON_CONFIGS})
        string(REPLACE - _ name ${config})
        set(validate VALIDATE)
        if(config MATCHES ".*snippet.*")
            # RTL snippets require an L2 preload file that is not in the source tree.
            set(validate)
        endif()
        add_panthera_test(cheetah_json_${name}_dryrun DRYRUN FILELESS ${validate}
            SPECIFICATION ${CMAKE_INSTALL_PREFIX}/config/cheetah/json/${config}.json
            ARGUMENTS --debug
            WORKLOADS ${TRACELIST}
            LABELS integration cheetah config veryslow)

        if(config MATCHES ".*_regression")
            set_property(TEST cheetah_json_${name}_dryrun PROPERTY COST 200)
        elseif(config MATCHES ".*_snippets_.*")
            set_property(TEST cheetah_json_${name}_dryrun PROPERTY COST 180)
        elseif(config MATCHES ".*_study")
            set_property(TEST cheetah_json_${name}_dryrun PROPERTY COST 25)
        endif()
    endforeach()
endif()

# Rollup config tests
add_python_test(rollup_config_integration TESTS test_rollup.py LABELS integration)

# EVP config tests
add_python_test(evp_config_integration TESTS test_evp.py LABELS integration)
