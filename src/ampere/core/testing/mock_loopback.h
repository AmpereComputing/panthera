//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include "ampere/sc/sc.h"

namespace ampere::core {

using namespace sc;

template<typename ReqType, typename RespType>
class MockLoopBack : public Module
{
public:
    explicit MockLoopBack(const sc_core::sc_module_name& name) : Module(name)
    {
        connect(loop_out, loop_in).name("loopback").latency(2);
    }

    In<ReqType> request{"request"};
    Out<RespType> response{"response"};
    int request_count{0};
    int max_concurrent_requests{0};

    bool run() override
    {
        while (request->can_take()) {
            loop_out->put(request->take());
            request_count++;
            current_request_count++;
            if (current_request_count > max_concurrent_requests)
                max_concurrent_requests = current_request_count;
        }

        while (loop_in->can_take()) {
            response->put(make_response(loop_in->take()));
            current_request_count--;
        }
        return true;
    }

protected:
    Out<ReqType> loop_out;
    In<ReqType> loop_in;
    int current_request_count{0};

private:
    virtual RespType make_response(ReqType request) = 0;
};

} // namespace ampere::core

