# Copyright (c) 2024 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

add_library(ampere_kestrel_raw
    mesh/aicl.cc
    mesh/aicl.h
    mesh/cluster.h
    mesh/slice.cc
    mesh/slice.h
    mesh/tile.cc
    mesh/tile.h
)

target_link_libraries(ampere_kestrel_raw ampere_mesh ampere_sc chi_headers)

add_library(ampere_kestrel INTERFACE)
target_link_libraries(ampere_kestrel INTERFACE "$<LINK_LIBRARY:WHOLE_ARCHIVE,ampere_kestrel_raw>")

add_subdirectory(test)
add_subdirectory(aice-sim)

