# Copyright (c) 2020-2024 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

add_library(uarch_common_raw
            cache.cc
            cache.h
            indexer.h
            memop.cc
            memop.h
            memreq.h
            memresp.h
            crp/cache_replacement_policy.h
            crp/lru.cc
            crp/lru.h
            crp/il1_lru.cc
            crp/il1_lru.h
            crp/fifo.cc
            crp/fifo.h
            crp/rr.cc
            crp/rr.h
            crp/plru.cc
            crp/plru.h
            crp/nmru.cc
            crp/nmru.h
            crp/rrip.cc
            crp/rrip.h
            crp/plru5.cc
            crp/plru5.h
            crp/plru6.cc
            crp/plru6.h)
target_link_libraries(uarch_common_raw PUBLIC ampere_sc)
add_library(uarch_common INTERFACE)
target_link_libraries(uarch_common INTERFACE "$<LINK_LIBRARY:WHOLE_ARCHIVE,uarch_common_raw>")
add_subdirectory(test)
