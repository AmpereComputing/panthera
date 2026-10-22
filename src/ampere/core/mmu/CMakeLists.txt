# Copyright (c) 2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

target_sources(ampere_core_raw PRIVATE
    ack.h
    l2tlb.h
    mru.h
    op.h
    otb.cc
    otb.h
    pwt.h
    req.h
    resp.h
    tpc.h
)

add_subdirectory(test)
