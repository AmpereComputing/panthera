//
// Copyright (c) 2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include "ampere/core/l2/l2c_reqqueue_prefetch.h"
#include "ampere/uarch_common/memop.h"

namespace ampere::core::l2 {

/* This function returns the next spot that should be written too in the circular queue
 * We try to find an invalid entry, if there isn't one we return the next entry from
 * where we are pointing at currently */
RequestQueuePrefetch::Buffer::iterator RequestQueuePrefetch::get_write_location()
{
    // Find first invalid entry, starting with the current scheduler pointer
    auto ptr = schedule_ptr;
    do {
        if (!*ptr)
            return ptr;

        if (++ptr == buffer.end())
            ptr = buffer.begin();
    } while (ptr != schedule_ptr);

    // If all entries are valid we increment the pointer to point to the next valid entry
    ptr = schedule_ptr;
    if (++ptr == buffer.end())
        ptr = buffer.begin();

    return ptr;
}

RequestQueuePrefetch::Buffer::iterator RequestQueuePrefetch::get_first_valid_entry()
{
    // Find first valid entry, starting with the current scheduler pointer
    auto ptr = schedule_ptr;
    do {
        if (*ptr)
            return ptr;
        if (++ptr == buffer.end())
            ptr = buffer.begin();
    } while (ptr != schedule_ptr);

    return schedule_ptr;
}

int RequestQueuePrefetch::request_priority(const MemReq& req) const
{
    return (req->origin == uarch_common::MemOp::Requester::LSU_PREFETCH
            && req->prefetch_type == uarch_common::MemOp::PrefetchType::NONE)
               ? -1 // treat LSU prefetches as highest priority
               : static_cast<int>(req->prefetch_type);
}

void RequestQueuePrefetch::insert(Op op)
{
    Cycle cycle = get_cycle();
    bool first_insert = (cycle != first_cycle); // Set if this is the first insertion of this cycle

    /* Old prefetch style used in Siryn product */
    if (!prefetch_favor_high_priority) {
        unsigned capacity = buffer.size();
        ops.push_back(op);
        if (ops.size() > capacity) {
            prefetcher->overflow(ops.front()->req);
            ops.pop_front();
        }
        return;
    }

    if (first_insert) {
        write_ptr = get_write_location();
        first_cycle = cycle;
    }

    // Write incoming prefetches to the request queue, but only if the new entry
    // is not a lower priority than whats already in that entry.
    // (Lower number is higher priority)
    auto incoming_priority = request_priority(op->req);

    if (!*write_ptr) {
        // Not valid
        *write_ptr = std::make_optional<Op>(op);
        if (first_insert)
            schedule_ptr = write_ptr;
    } else if (incoming_priority <= request_priority((**write_ptr)->req)) {
        // Higher priority prefetch replaces old prefetch
        if (**write_ptr)
            prefetcher->overflow((**write_ptr)->req);
        *write_ptr = std::make_optional<Op>(op);
        if (first_insert)
            schedule_ptr = write_ptr;
    } else {
        // Prefetch buffer entry is full and new request is lower priority, drop it
        prefetcher->overflow(op->req);
    }

    // Move write pointer to next location for next request
    if (++write_ptr == buffer.end())
        write_ptr = buffer.begin();
}

ScheduledOp RequestQueuePrefetch::schedule(ScheduleCheck okay_to_schedule)
{
    if (!prefetch_favor_high_priority)
        return RequestQueue::schedule(okay_to_schedule);

    int considered{0};

    if (*schedule_ptr) {
        auto ptr = schedule_ptr;
        do {
            if (*ptr) {
                std::optional<Op> itr = *ptr;
                Op op = *itr;
                if (okay_to_schedule(queue_name, op).read) {
                    *ptr = {};
                    if (ptr == schedule_ptr)
                        schedule_ptr = get_first_valid_entry();
                    return {true, false, false, op};
                }
            }

            if (++ptr == buffer.end())
                ptr = buffer.begin();
        } while (ptr != schedule_ptr && ++considered < depth);
    }

    schedule_ptr = get_first_valid_entry();
    return {};
}

void RequestQueuePrefetch::dump(std::ostream& os = std::cout) const
{
    os << "\nPrefetch Request Queue Status:" << std::endl;
    os << "-----------------------------------------------------------------" << std::endl;
    os << "| Index | Valid |   Address  | Schedule Pointer | Write Pointer |" << std::endl;
    os << "-----------------------------------------------------------------" << std::endl;

    int idx = 0;
    for (auto l = buffer.begin(); l != buffer.end(); l++, idx++) {
        const char* is_schedule_ptr = l == schedule_ptr ? "*" : "";
        const char* is_write_ptr = l == write_ptr ? "*" : "";
        char valid = l->has_value() ? 'Y' : 'N';
        PhysAddr addr = l->has_value() ? (**l)->req->phys_addr : 0_pa;

        fmt::print("| {:>5d} | {:>5} | {:10} | {:^16} | {:^13} |\n", idx, valid, addr, is_schedule_ptr, is_write_ptr);
    }
    os << "-----------------------------------------------------------------\n" << std::endl;
}
} // namespace ampere::core::l2
