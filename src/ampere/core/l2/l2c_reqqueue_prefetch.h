//
// Copyright (c) 2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#pragma once

#include <boost/circular_buffer.hpp>
#include "ampere/core/l2/l2c_reqqueue.h"
#include "ampere/core/prefetcher.h"

namespace ampere::core::l2 {

class RequestQueuePrefetch : public RequestQueue
{
public:
    Param<bool> prefetch_favor_high_priority{
        "prefetch_favor_high_priority", "New prefetch buffer management algorithm for Ripley", false};

    RequestQueuePrefetch(const sc_core::sc_module_name &name,
                         l2::RequestQueueName queue_name,
                         int depth,
                         Prefetcher<MemReq> *prefetcher,
                         unsigned capacity)
        : RequestQueue(name, queue_name, depth), prefetcher{prefetcher}
    {
        buffer.resize(capacity, std::nullopt);
        schedule_ptr = buffer.begin();
        write_ptr = buffer.begin();
    }

    void insert(Op op);
    ScheduledOp schedule(ScheduleCheck okay_to_schedule) override;

    void dump(std::ostream &os) const override;

private:
    Cycle first_cycle{-1};
    Prefetcher<MemReq> *prefetcher;

    using Buffer = boost::circular_buffer<std::optional<Op>>;

    Buffer buffer;
    Buffer::iterator schedule_ptr;
    Buffer::iterator write_ptr;

    Buffer::iterator get_write_location();
    Buffer::iterator get_first_valid_entry();

    int request_priority(const MemReq &req) const;
};
} // namespace ampere::core::l2
