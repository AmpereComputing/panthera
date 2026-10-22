//
// Copyright (c) 2019-2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/core/prefetchers/l1p_oracle.h"
#include "ampere/core/backend_prefetcher.h"

namespace ampere::core {

template<>
BackendMemReq OraclePrefetcher<BackendMemReq, BackendPrefetcher<BackendMemReq>>::make_prefetch_request(
    BackendMemReq req, VirtAddr addr, bool prefetch_into_requester)
{
    return {addr, {}, req.uop, this->prefetch_type()};
}

template<>
uint64_t OraclePrefetcher<BackendMemReq, BackendPrefetcher<BackendMemReq>>::request_access_hint(BackendMemReq req) const
{
    return req.uop ? req.uop->ainst()->inum().id() : 0;
}

template<>
bool OraclePrefetcher<BackendMemReq, BackendPrefetcher<BackendMemReq>>::should_train(BackendMemReq req) const
{
    return true;
}

template<>
bool OraclePrefetcher<BackendMemReq, BackendPrefetcher<BackendMemReq>>::is_code_request(BackendMemReq req) const
{
    return false;
}

L1P_Oracle::L1P_Oracle(sc_core::sc_module_name name) : OracleBackendPrefetcher<BackendMemReq>(name) {}

} // namespace ampere::core

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
