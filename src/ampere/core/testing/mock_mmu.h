//
// Copyright (c) 2018-2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include "ampere/sc/sc.h"
#include "ampere/core/flush.h"
#include "ampere/core/mmu/req.h"
#include "ampere/core/mmu/ack.h"
#include "ampere/core/mmu/resp.h"
#include "ampere/core/tlbireq.h"
#include "ampere/sc/testing/test_interfaces.h"

namespace ampere::core {

using namespace sc;

using MockMMUReqWriter = MockPutter<MMUReq>;
using MockMMUReqReader = MockTaker<MMUReq>;

using MockMMUAckWriter = MockPutter<MMUAck>;
using MockMMUAckReader = MockTaker<MMUAck>;

using MockMMURespWriter = MockPutter<MMUResp>;
using MockMMURespReader = MockTaker<MMUResp>;

using MockTLBIReqWriter = MockPutter<TLBIReq>;
using MockTLBIReqReader = MockTaker<TLBIReq>;

class MMUDummy : public Module
{
public:
    explicit MMUDummy(const sc_core::sc_module_name& name) : Module(name)
    {
        connect(loop_out, loop_in).name("loopback");
    }

    In<MMUReq> request{"request"};
    Out<MMUResp> response{"response"};
    Out<MMUAck> ack{"ack"};
    Read<Flush> flush{"flush"};
    int request_count{0};
    int max_concurrent_requests{0};

    bool run() override;

protected:
    Out<MMUReq> loop_out;
    In<MMUReq> loop_in;
    int current_request_count{0};

private:
    MMUResp make_response(MMUReq request);
    void handle_flush();
    std::map<uint64_t, MMUReq> in_flight;
    std::map<uint64_t, bool> flushed;
};

} // namespace ampere::core

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
