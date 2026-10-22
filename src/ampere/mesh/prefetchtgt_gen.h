//
// Copyright (c) 2024 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#pragma once

#include "ampere/mesh/mesh_dat.h"
#include "ampere/sc/sc.h"

make_ns_enum(ampere::mesh, DatSrcAcc, OFF, STARTUP, ACTIVE);

namespace ampere::mesh {

using namespace ampere::sc;

class PrefetchTgtGen : public Module
{
public:
    explicit PrefetchTgtGen(const sc_core::sc_module_name& name) : Module{name} {}

    void accumulate(MeshDat dat);
    bool active() const { return dat_src_acc == DatSrcAcc::ACTIVE; }

private:
    Param<CycleDelta> pft_off_cyc_threshold{
        "pft_off_cyc_threshold", "Number of cycles to stay in OFF state before attempting prefetches", 1 << 5};
    Param<int> startup_wait_threshold{
        "startup_wait_threshold", "Number of data packets to ignore before going active", 1 << 10};
    Param<int> rxdat_window{
        "rxdat_window", "Window over which to collect success counts (in number of data packets)", 1 << 10};
    Param<int> efficiency{
        "efficiency", "Minimum efficiency (in successful prefetches per window) required to remain active", 1 << 9};
    Param<bool> accum_in_startup{"accum_in_start", "Count successful prefetches during the startup phase", false};

    DatSrcAcc dat_src_acc{DatSrcAcc::OFF};

    Cycle last_acc_cycle{0};
    CycleDelta pft_off_cyc_count{0};
    int startup_wait_count{0};
    int rxdat_count{0};
    int rxdat_pft_successful_count{0};
};

} // namespace ampere::mesh
