# Copyright (c) 2024 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

add_library(tigris_lib tigris.cc tigris.h)
target_link_libraries(tigris_lib PUBLIC ampere_core ampere_sc archx fmt)

add_executable(tigris main.cc)
target_link_libraries(tigris tigris_lib)
set_target_properties(tigris PROPERTIES INSTALL_RPATH_USE_LINK_PATH TRUE)

install(TARGETS tigris DESTINATION bin)

add_subdirectory(test)
