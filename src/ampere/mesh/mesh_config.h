//
// Copyright (c) 2020-2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#pragma once
#include "ampere/mesh/chi/types.h"
#include "ampere/soc/warmup_info.h"

#include <sysc/communication/sc_interface.h>

namespace ampere::mesh {

class MeshConfigIfc : public sc_core::sc_interface
{
public:
    virtual size_t get_num_die() const { return 0; }
    virtual int data_bus_width() const = 0;
    virtual int channel(const chi::NodeId id) const = 0;
    virtual std::vector<chi::NodeId> stash_nodes(uint8_t die_id) const { return {}; }
    virtual std::set<chi::NodeId> pmha_nodes(void) const { return {}; }

    // For warmup: this function allows a core/injector to announce that it has warm-up transactions.
    // I don't know if this should really be part of MeshConfigIfc, but for prototyping this is the
    // most expedient place to put it.
    //
    // The parameters are:
    //
    //     num_transactions  - how many warm up transactions there are (for scheduling interleaving)
    //     gen_warmup        - callback function to generate one transaction
    //
    virtual void register_warmup(uint64_t num_transactions, std::function<std::optional<soc::WarmupInfo>()> gen_warmup)
    {
    }
};

using MeshConfigPort = sc_core::sc_port<MeshConfigIfc>;

} // namespace ampere::mesh

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
