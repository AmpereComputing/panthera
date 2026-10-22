# Copyright (c) 2022-2023 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

set(pyext_SRCS
    "adis-pyext.cc"
)
add_library(pyext SHARED ${pyext_SRCS})

set_target_properties(
    pyext
    PROPERTIES
        PREFIX ""
        OUTPUT_NAME ${pyext_MODULE_NAME}
        LINKER_LANGUAGE CXX
)


target_include_directories(pyext PUBLIC
    ${Python3_INCLUDE_DIRS}
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_BINARAY_DIR}
    )

target_link_libraries(pyext adis Python3::Python)

install(TARGETS pyext DESTINATION lib/python)
