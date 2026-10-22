# Copyright (c) 2020-2026 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

add_library(ampere_memory_raw
            bandwidth_latency.cc
            bandwidth_latency.h
            chp.cc
            chp.h
            hif.h
            memory.cc
            memory.h
            mem_responder.cc
            mem_responder.h)
target_link_libraries(ampere_memory_raw PUBLIC ampere_mesh ampere_sc uarch_common)

add_library(ampere_memory INTERFACE)
target_link_libraries(ampere_memory INTERFACE "$<LINK_LIBRARY:WHOLE_ARCHIVE,ampere_memory_raw>")

add_subdirectory(test)
