//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/core/prefetchers/l2p_nextline.h"
#include "ampere/uarch_common/memreq.h"

namespace ampere::core {

L2P_NextLine::L2P_NextLine(sc_core::sc_module_name name) : Prefetcher<MemReq>(name) {}

// Only for computing precise stats
MemOp::PrefetchType L2P_NextLine::prefetch_type() const
{
    return MemOp::PrefetchType::NEXTLINE;
}

void L2P_NextLine::train_impl(MemReq req, bool hit, bool prefetch, bool demand)
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
            auto pref_addr = req->phys_addr.next_line_address();

            // crossing the page boundary, no prefetch request issued
            if (!pref_addr.same_page(req->phys_addr, req->page_size)) {
                return;
            }
            auto prefetch_req = make_child<MemReq>(req,
                                                   MemReqObj::prefetch(pref_addr.value(),
                                                                       pref_addr,
                                                                       req->virt_addr.next_line_address(),
                                                                       MemOp::Requester::PREFETCH,
                                                                       prefetch_type(),
                                                                       req->page_size));
            prefetch_request->put(prefetch_req);
        }
    }
}

void L2P_NextLine::table_print(std::ostream &os)
{
    return;
}

} // namespace ampere::core

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
