/*
 * Copyright (c) 2024-2025 Ampere Computing. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "ampere/core/prefetchers/oracle.h"
#include "ampere/uarch_common/memop.h"
#include "ampere/uarch_common/memreq.h"

namespace ampere::core {

template<>
Fetch OraclePrefetcher<Fetch>::make_prefetch_request(Fetch req, ProgramCounter addr, bool prefetch_into_requester)
{
    return make_child<Fetch>(req, addr, 0);
}

template<>
uint64_t OraclePrefetcher<Fetch>::request_access_hint(Fetch req) const
{
    return req->fetch_id;
}

template<>
bool OraclePrefetcher<Fetch>::is_code_request(Fetch req) const
{
    return true;
}

template<>
bool OraclePrefetcher<Fetch>::should_train(Fetch req) const
{
    return true;
}

template<>
MemReq OraclePrefetcher<MemReq>::make_prefetch_request(MemReq req, PhysAddr addr, bool prefetch_into_requester)
{
    return make_child<MemReq>(req,
                              MemReqObj::prefetch(addr.value(),
                                                  addr,
                                                  prefetch_into_requester ? req->origin : MemOp::Requester::PREFETCH,
                                                  MemOp::PrefetchType::NONE,
                                                  req->page_size));
}

template<>
uint64_t OraclePrefetcher<MemReq>::request_access_hint(MemReq req) const
{
    return req->id;
}

template<>
bool OraclePrefetcher<MemReq>::is_code_request(MemReq req) const
{
    return req->origin == uarch_common::MemOp::ICF;
}

template<>
bool OraclePrefetcher<MemReq>::should_train(MemReq req) const
{
    return !req->from_prefetch
           && ((include_code && req->origin == uarch_common::MemOp::ICF)
               || (include_data
                   && (req->origin == uarch_common::MemOp::LSU || req->origin == uarch_common::MemOp::LSU_PREFETCH)));
}

CP_Oracle::CP_Oracle(sc_core::sc_module_name name) : OraclePrefetcher<Fetch>(name) {}

L2P_Oracle::L2P_Oracle(sc_core::sc_module_name name) : OraclePrefetcher<MemReq>(name) {}

} // namespace ampere::core

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
