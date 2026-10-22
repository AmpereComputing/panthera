# Copyright (c) 2024 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

set(sources
    instruction_consumer.cc
    instruction_generator.cc
    mock_coherency_validator.h
    mock_flush.h
    mock_indexer.h
    mock_l2.cc
    mock_loopback.h
    mock_mmu.cc
    mock_prefetcher.h
    pc_consumer.cc
    pc_generator.cc
    uop_consumer.cc
    uop_generator.cc
    uops.cc
    uops.h)

add_library(core_testing ${sources})
target_link_libraries(core_testing PUBLIC ampere_core gtest gmock)
