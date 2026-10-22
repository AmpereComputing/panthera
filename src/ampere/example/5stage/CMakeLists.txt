# Copyright (c) 2018 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

add_executable(5stage 5stage.cc)
target_link_libraries(5stage ampere_sc)
