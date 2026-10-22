//
// Copyright (c) 2018-2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/core/testing/mock_mmu.h"

namespace ampere::core {

bool MMUDummy::run()
{
    while (request->can_take()) {
        MMUReq mmureq = request->take();
        in_flight.emplace(mmureq->id, mmureq);
        loop_out->put(mmureq);
        request_count++;
        current_request_count++;
        if (current_request_count > max_concurrent_requests)
            max_concurrent_requests = current_request_count;

        MMUAck mmuack = make_child<MMUAck>(mmureq, mmureq->id, mmureq->id);
        ack->put(mmuack);
    }

    if (flush->can_read())
        handle_flush();

    while (loop_in->can_take()) {
        MMUReq mmureq = loop_in->take();
        current_request_count--;
        if (flushed.contains(mmureq->id)) {
            flushed.erase(mmureq->id);
        } else {
            response->put(make_response(mmureq));
        }
        in_flight.erase(mmureq->id);
    }
    return true;
}

MMUResp MMUDummy::make_response(MMUReq request)
{
    VirtAddr va(request->virt_addr);
    PhysAddr pa(request->virt_addr.value());
    return make_child<MMUResp>(request,
                               request->id,
                               va,
                               request->is_instruction,
                               request->is_store,
                               pa,
                               PageSize::Size::PS_4KB,
                               10 /*ASID*/,
                               false);
}

void MMUDummy::handle_flush()
{
    auto flush_info = flush->read();

    for (auto& req_handle : in_flight) {
        bool is_instr = req_handle.second->is_instruction;
        if (is_instr || req_handle.first > flush_info->alloc_id) {
            flushed[req_handle.first] = true;
        }
    }
}

} // namespace ampere::core

