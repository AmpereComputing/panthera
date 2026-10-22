# Copyright (c) 2021 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

add_executable(4unit 4unit.cc)
target_link_libraries(4unit PUBLIC ampere_sc uarch_common)
