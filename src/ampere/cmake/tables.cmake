# Copyright (c) 2021-2024 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

set(python_path "${PROJECT_SOURCE_DIR}/src/ampere/python")
set(tables_pkg_files "${python_path}/panthera/tables/__main__.py" "${python_path}/panthera/tables/__init__.py")

function(tab2enum)
    cmake_parse_arguments(tab2enum "" "TARGET;TAB;HFILE;CCFILE;PYTHON" "" ${ARGN})

    get_filename_component(name "${tab2enum_TAB}" NAME_WLE)
    if (NOT DEFINED tab2enum_TARGET)
        set(tab2enum_TARGET "${name}_enum")
    endif()
    if (NOT DEFINED tab2enum_HFILE)
        set(tab2enum_HFILE "${name}.enum.h")
    endif()
    if (NOT DEFINED tab2enum_CCFILE)
        set(tab2enum_CCFILE "${name}.enum.cc")
    endif()
    if (NOT DEFINED tab2enum_PYTHON)
        set(tab2enum_PYTHON "${name}.py")
    endif()

    set(args -m panthera.tables "${CMAKE_CURRENT_SOURCE_DIR}/${tab2enum_TAB}" -H ${tab2enum_HFILE} -c ${tab2enum_CCFILE})
    if (tab2enum_PYTHON)
        list(APPEND args -p ${tab2enum_PYTHON})
    endif()

    add_custom_command(
        OUTPUT ${tab2enum_HFILE} ${tab2enum_CCFILE} ${tab2enum_PYTHON}
        COMMAND env PYTHONPATH=$$PYTHONPATH:${python_path} ${Python3_EXECUTABLE} ${args}
        DEPENDS ${tab2enum_TAB} ${tables_pkg_files} "${python_path}/panthera/tables/c_out.py" "${python_path}/panthera/tables/py_out.py"
    )
    add_custom_target(${tab2enum_TARGET} DEPENDS ${tab2enum_HFILE} ${tab2enum_CCFILE} ${tab2enum_PYTHON})
    set_source_files_properties(${tab2enum_HFILE} ${tab2enum_CCFILE} PROPERTIES SKIP_LINTING ON)
endfunction()
