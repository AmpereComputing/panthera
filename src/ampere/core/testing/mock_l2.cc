//
// Copyright (c) 2018-2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/core/testing/mock_l2.h"

namespace ampere {
namespace core {

L2Dummy::L2Dummy(const sc_core::sc_module_name& name) : L2DummyBase(name) {}

MemResp L2Dummy::make_response(MemReq request)
{
    return make_child<MemResp>(request, request->id, request->phys_addr, true);
}

ObservableL2Dummy::ObservableL2Dummy(const sc_core::sc_module_name& name) : L2DummyBase(name) {}

MemResp ObservableL2Dummy::make_response(MemReq request)
{
    auto response = make_child<MemResp>(request, request->id, request->phys_addr, true);
    if (observable_request->can_put())
        observable_request->put(request);
    if (observable_response->can_put())
        observable_response->put(response);
    return response;
}

L2InclusiveDummy::L2InclusiveDummy(const sc_core::sc_module_name& name) : L2InclusiveDummyBase(name) {}

MemResp L2InclusiveDummy::make_response(Request::Fill request_pair)
{
    if (request_scheduled.bind_count() > 0 && request_scheduled->can_put()) {
        request_scheduled->put(request_pair.first);
    }
    return make_child<MemResp>(request_pair.first, request_pair.first->id, request_pair.first->phys_addr, true);
}

} // namespace core
} // namespace ampere

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
