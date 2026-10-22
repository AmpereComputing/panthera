# Copyright (c) 2023-2026 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

add_library(effolkronium_random INTERFACE)

add_subdirectory(ampere)
add_subdirectory(elfio)
set_source_files_properties(DIRECTORY elfio PROPERTIES SKIP_LINTING ON)
