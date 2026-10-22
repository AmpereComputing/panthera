//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/core/prefetchers/l2p_marvel.h"

namespace ampere::core {

L2P_Marvel::L2P_Marvel(sc_core::sc_module_name name) : Prefetcher(name)
{
    connect(vldp.prefetch_request, vldp_request).name("vldp_request").latency(0).bandwidth(get_bandwidth());
    connect(bop.prefetch_request, bop_request).name("bop_request").latency(0).bandwidth(get_bandwidth());
}

void L2P_Marvel::train_impl(
    uint64_t index, MemReq req, bool hit, bool prefetch, bool demand, MemOp::PrefetchType prefetch_type)
{
    vldp.train_impl(req, hit, prefetch, demand);
    bop.train_impl(index, req, hit, prefetch, demand, prefetch_type);

    // Arbitration happens here,  First insert any vldp prefetch requests,
    // followed by bop requests if port has bandwidth
    while (vldp_request->can_take()) {
        auto vldp_req = vldp_request->take();
        if (prefetch_request->can_put()) {
            prefetch_request->put(vldp_req);
        }
    }

    while (bop_request->can_take()) {
        auto bop_req = bop_request->take();
        if (prefetch_request->can_put()) {
            prefetch_request->put(bop_req);
        }
    }
}

void L2P_Marvel::fill_impl(uint64_t index, MemReq req, bool fb_pref, bool was_alloc_by_pf)
{
    bop.fill_impl(index, req, fb_pref, was_alloc_by_pf);
}

} // namespace ampere::core

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
