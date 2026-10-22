# Copyright (c) 2024-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

add_library(ampere_sc_testing INTERFACE test_interfaces.h test_module.h test.h)
target_link_libraries(ampere_sc_testing INTERFACE event_stream_testing)
