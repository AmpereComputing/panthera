# Copyright (c) 2023-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

target_sources(ampere_core_raw PRIVATE
    l2p_bop.h
    l2p_bop.cc
    l2p_nextline.h
    l2p_nextline.cc
    l2p_spatial.h
    l2p_spatial.cc
    l2p_vldp.cc
    l2p_vldp.h
    l2p_spp.h
    l2p_spp.cc
    l2p_marvel.h
    l2p_marvel.cc
    l1p_pcp.h
    l1p_pcp.cc
    l1p_gretch.h
    l1p_gretch.cc
    l1p_bop.h
    l1p_bop.cc
    l1p_multiple.h
    l1p_multiple.cc
    l1p_l2p_translator.h
    l1p_l2p_translator.cc
    l1p_oracle.h
    l1p_oracle.cc
    cp_nextline.h
    cp_nextline.cc
    cp_sn4l.h
    cp_sn4l.cc
    cp_entangling.h
    cp_entangling.cc
    cp_mana.h
    cp_mana.cc
    multiple.h
    multiple.cc
    oracle.h
    oracle.cc
)

add_subdirectory(test)
