//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/core/prefetchers/l2p_spatial.h"

namespace ampere::core {

L2P_Spatial::L2P_Spatial(sc_core::sc_module_name name) : Prefetcher<MemReq>(name) {}

// Only for computing precise stats
MemOp::PrefetchType L2P_Spatial::prefetch_type() const
{
    return MemOp::PrefetchType::SPATIAL;
}

void L2P_Spatial::train_impl(MemReq req, bool hit, bool prefetch, bool demand)
{
    // Train on everything that comes in, including I-side
    switch (req->origin) {
    case MemOp::Requester::LSU:
    case MemOp::Requester::LSU_PREFETCH:
    case MemOp::Requester::DMMU:
    case MemOp::Requester::L2C:
    case MemOp::Requester::PREFETCH:
    case MemOp::Requester::ICF:
    case MemOp::Requester::IMMU:
        break;
    default:
        return;
    }

    if (!hit || prefetch) {
        if (prefetch_request->can_put()) {
            auto pref_addr = PhysAddr{req->phys_addr.value() ^ PhysAddr::LINE_SIZE}.line_address();
            auto prefetch_req = make_child<MemReq>(
                req,
                MemReqObj::prefetch(pref_addr.value(),
                                    pref_addr,
                                    VirtAddr{req->virt_addr.value() ^ PhysAddr::LINE_SIZE}.line_address(),
                                    MemOp::Requester::PREFETCH,
                                    prefetch_type(),
                                    req->page_size));
            prefetch_request->put(prefetch_req);
        }
    }
}

void L2P_Spatial::table_print(std::ostream &os)
{
    return;
}

} // namespace ampere::core

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
