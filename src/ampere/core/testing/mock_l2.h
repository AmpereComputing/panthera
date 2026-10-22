//
// Copyright (c) 2018-2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include "ampere/sc/sc.h"
#include "ampere/core/testing/mock_loopback.h"
#include "ampere/uarch_common/memreq.h"
#include "ampere/uarch_common/memresp.h"
#include "ampere/sc/testing/test_interfaces.h"

#include <gmock/gmock.h>

namespace ampere::core {

using namespace uarch_common;
using uarch_common::Request::Fill;

MATCHER_P(MemReqMatcher, cmpreq, "")
{
    return ((arg->id == cmpreq->id) && (arg->phys_addr == cmpreq->phys_addr));
}
MATCHER_P(MemReqAndVictimMatcher, cmpreq, "")
{
    return ((arg.first->id == cmpreq->id) && (arg.first->phys_addr == cmpreq->phys_addr));
}
MATCHER_P(MemRespMatcher, cmpresp, "")
{
    return ((arg->id == cmpresp->id) && (arg->phys_addr == cmpresp->phys_addr) && (arg->value == cmpresp->value));
}

using MockMemReqReader = MockTaker<MemReq>;
using MockMemReqWriter = MockPutter<MemReq>;

using MockMemReqAndVictimWriter = MockPutter<Fill>;
using MockMemReqAndVictimReader = MockTaker<Fill>;

using MockMemRespWriter = MockPutter<MemResp>;
using MockMemRespReader = MockTaker<MemResp>;

using MockBoolReader = MockTaker<bool>;
using MockBoolWriter = MockPutter<bool>;

using MockPhysAddrWriter = MockPutter<PhysAddr>;

using L2DummyBase = MockLoopBack<MemReq, MemResp>;
class L2Dummy : public L2DummyBase
{
public:
    explicit L2Dummy(const sc_core::sc_module_name& name);

private:
    MemResp make_response(MemReq request) override;
};

class ObservableL2Dummy : public L2DummyBase
{
public:
    explicit ObservableL2Dummy(const sc_core::sc_module_name& name);
    Out<MemReq> observable_request{"observable_request"};
    Out<MemResp> observable_response{"observable_response"};

private:
    MemResp make_response(MemReq request) override;
};

using L2InclusiveDummyBase = MockLoopBack<Fill, MemResp>;
class L2InclusiveDummy : public L2InclusiveDummyBase
{
public:
    explicit L2InclusiveDummy(const sc_core::sc_module_name& name);
    OutOpt<MemReq> request_scheduled{"request_scheduled"};

private:
    MemResp make_response(Fill request_pair) override;
};

} // namespace ampere::core

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
