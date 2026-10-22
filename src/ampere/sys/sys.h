//
// Copyright (c) 2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#pragma once

#include "ampere/mesh/pager.h"
#include "ampere/sc/sc.h"
#include "ampere/soc/soc.h"

namespace ampere {

using namespace ampere::sc;

class Sys : public Module
{
public:
    explicit Sys(const sc_core::sc_module_name& name) : Module{name} {}
    Sys(const sc_core::sc_module_name& name, Clock* clock, Environment* env) : Module{name, clock, env} {}

private:
    Param<size_t> num_socs{"num_socs", "number of SoCs", 1};

    Event sim_end_event{"sim_end", "Simulation has ended"};
    EventData<uint64_t> total_cycles{"total_cyles", "Cycles completed in sim"};

    mesh::RandomPager pager{"pager"};

    sc_core::sc_vector<Submodule<soc::SocBase, soc::Soc>> soc{"soc", num_socs};

    void end_of_simulation() override
    {
        Cycle end = get_cycle();
        sim_end_event(total_cycles(end));
    }
};

} // namespace ampere

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
