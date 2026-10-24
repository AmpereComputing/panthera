//
// Copyright (c) 2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include "ampere/core/l2/l2c.h"
#include "ampere/core/l2/l2c_op.h"
#include "ampere/core/l2/l2c_opcode.enum.h"
#include "ampere/core/l2/l2c_reqqueue.h"
#include "ampere/uarch_common/memop.h"

namespace ampere::core {

/** Build the table of request queues to schedule from.
 *
 *  There are a few different orders in which we might visit the queues (higher priority
 *  queues come earlier). To make the scheduling routine a little neater, here
 *  we pre-arrange the queues into the different orders they might require.
 *  That way, we can index into the top level with a choice of priority and we'll
 *  have all the queues laid out in the right order.
 */
void L2C::build_requests_by_priority()
{
    requests = {// normal priority
                {&snpq,
                 &srq_fill,
                 &srq_dealloc,
                 &lsu_evict_prq,
                 &icf_prq,
                 &immu_prq,
                 &dmmu_prq,
                 &lsu_read_prq,
                 &prefetch_prq,
                 &pwc},
                // "stores highest" priority
                {&snpq,
                 &srq_fill,
                 &srq_dealloc,
                 &lsu_evict_prq,
                 &pwc,
                 &icf_prq,
                 &immu_prq,
                 &dmmu_prq,
                 &lsu_read_prq,
                 &prefetch_prq}};
}

/** Choose the first eligible request (if any) from a list of queues.
 *
 *  The queues are in priority order, so we visit each one looking for a request
 *  we can schedule. Then we look into each queue as far back as "depth" allows
 *  us (notice that depth == -1 means look through the whole queue).
 *
 *  We'll pick (and remove from the queue) one "read" and one "write" operation
 *  if possible.
 */
void L2C::arbitrate()
{
    bool writes_highest_priority = pwc.get_priority_timer() >= write_priority_age_threshold;
    int priority = writes_highest_priority ? 1 : 0;
    std::vector<Op> bank_unable_to_schedule(num_banks, nullptr);

    auto can_sched = std::bind_front(&L2C::can_schedule, this);

    for (auto& rq : requests[priority]) {
        // We skip the prefetch queue if we are throttling based on cbusy feedback
        if (rq->queue_name == l2::RequestQueueName::PREFETCH && cbusy.prefetcher_throttled())
            continue;

        // Get operation that can be scheduled
        auto winner = rq->schedule(can_sched);

        // Schedule Operation
        if (winner.op) {
            auto& op = winner.op;
            if (winner.any()) {
                auto read_busy_mask = get_read_busy_mask(op->opcode);
                auto write_busy_mask = get_write_busy_mask(op->opcode);

                if (op->opcode == l2::Opcode::READ_DEALLOC)
                    last_read_dealloc_cycle = get_cycle();

                if (winner.write) {
                    write_op = op;
                    write_op->fb_credits = banks[write_op->bank].schedule(write_op, write_busy_mask);
                    event.write_schedule(write_op, event.bank(write_op->bank));

                    if (is_pwc_write_op(op))
                        banks[write_op->bank].in_flight_stores.update(op->req->barrier_type, op->req->atomic_type, 1);

                    // Some ops, like FILL, need both pipes' resources. But of course we only really want
                    // to execute it in one pipe. So only set up the write_op, but stop scheduling.
                    if (winner.both()) {
                        event.read_schedule(write_op, event.bank(write_op->bank));
                        break;
                    }
                } else if (winner.read) {
                    read_op = op;
                    read_op->fb_credits = banks[read_op->bank].schedule(read_op, read_busy_mask);
                    event.read_schedule(read_op, event.bank(read_op->bank));
                } else if (winner.sys) {
                    sys_op = op;
                    event.sys_schedule(sys_op, event.bank(sys_op->bank));
                }
            } else if (auto& last = bank_unable_to_schedule[op->bank]; !last) {
                last = op;
            }

            if (read_op && write_op && (!sys_pipe_dealloc || sys_op))
                break;
        }
    }

    event.arbitrate();

    // A read which schedules at the same time as a fill remembers the address of the fill,
    // in case we need to cancel the fill.
    if (write_op && write_op->opcode == l2::Opcode::FILL && read_op)
        read_op->scheduled_with_fill = write_op->addr;

    if (!read_op && !write_op) {
        bool any_unscheduled = false;
        bool fb_full_bank_stall = false;
        for (size_t i = 0; i < num_banks; ++i) {
            const auto& op = bank_unable_to_schedule[i];
            any_unscheduled |= static_cast<bool>(op);
            if (!op || !banks[i].is_available() || !needs_fill_buffer(op->opcode))
                continue;
            // If we get here, then we know that an op was unscheduled due to unavailability of an FB entry
            event.fb_full(op->req);
            event.pmu(op, event.pmu_num(archx::PMU::L2C_FBFULL_BNKSTALL));
            fb_full_bank_stall = true;
        }
        if (any_unscheduled) {
            event.needed_banks_busy();
            if (fb_full_bank_stall)
                // Ops are waiting to schedule, but no op was scheduled this cycle, and at least one op was not
                //  scheduled due to unavailability of an FB entry
                event.pmu(event.pmu_num(archx::PMU::L2C_FBFULL_SCHSTALL));
        }
    }
}

PipelineArbitration L2C::op_bus_needs(l2::Opcode opcode) const
{
    switch (opcode) {
    case l2::Opcode::FILL:
    case l2::Opcode::ATOMIC_LOAD:
    case l2::Opcode::ATOMIC_CAS:
    case l2::Opcode::ATOMIC_STORE:
        return {true, true, false};
    case l2::Opcode::FILL_FIRSTFLIT:
        return {first_half_fill_blocks_read, true, false};
    case l2::Opcode::SUBWR:
    case l2::Opcode::SUBWREXCL:
        return {false, true, false};
    case l2::Opcode::FB_DEALLOC:
    case l2::Opcode::READ_DEALLOC:
    case l2::Opcode::READ_VICTIM:
        return {!sys_pipe_dealloc, false, sys_pipe_dealloc};
    default:
        return {true, false, false};
    }
}

uint64_t L2C::get_read_busy_mask(l2::Opcode opcode) const
{
    switch (opcode) {
    case l2::Opcode::FB_DEALLOC:
        return sys_pipe_dealloc ? 0x0 : 0x1;
    case l2::Opcode::READ_DEALLOC:
    case l2::Opcode::READ_VICTIM:
        return sys_pipe_dealloc ? 0x0 : bank_read_dealloc_busy_mask;
    case l2::Opcode::ATOMIC_LOAD:
    case l2::Opcode::ATOMIC_STORE:
    case l2::Opcode::ATOMIC_CAS:
        return bank_atomic_busy_mask;
    default:
        return bank_read_busy_mask;
    }
}

uint64_t L2C::get_write_busy_mask(l2::Opcode opcode) const
{
    switch (opcode) {
    case l2::Opcode::FILL_FIRSTFLIT:
        return first_half_fill_blocks_read ? bank_write_busy_mask : 0x1ULL;
    case l2::Opcode::ATOMIC_LOAD:
    case l2::Opcode::ATOMIC_STORE:
    case l2::Opcode::ATOMIC_CAS:
        return bank_atomic_busy_mask;
    default:
        return bank_write_busy_mask;
    }
}

bool L2C::address_within_bank(PhysAddr addr) const
{
    int bank_id = banks.select(addr);
    if (fb.has_request_in_bank(addr, bank_id))
        return true;

    if (std::any_of(read_pipe.begin(), read_pipe.end(), [&](const auto& op) {
            return op->addr == addr && op->bank == bank_id;
        }))
        return true;

    if (std::any_of(write_pipe.begin(), write_pipe.end(), [&](const auto& op) {
            return op->addr == addr && op->bank == bank_id;
        }))
        return true;

    return false;
}

PipelineArbitration L2C::can_schedule(l2::RequestQueueName queue_name, l2::Op op) const
{
    if (queue_name == l2::RequestQueueName::IMMU && immu_duplicate_filter.contains(filter_address(op->addr)))
        return {};
    if (queue_name == l2::RequestQueueName::DMMU && dmmu_duplicate_filter.contains(filter_address(op->addr)))
        return {};

    // READ_DEALLOC can only schedule every other cycle
    if (op->opcode == l2::Opcode::READ_DEALLOC && last_read_dealloc_cycle == get_cycle() - 1)
        return {};

    auto [needs_read, needs_write, needs_sys] = op_bus_needs(op->opcode);

    if (!fills_without_dest_need_read_pipe && op->opcode == l2::Opcode::FILL && !fb[op->fbid].returns_to_core())
        needs_read = false;

    bool busses_free = (!read_op || !needs_read) && (!write_op || !needs_write) && (!sys_op || !needs_sys);
    auto bank_id = banks.select(op->addr);
    const auto& bank = banks[bank_id];

    if (queue_name == l2::RequestQueueName::DMMU || queue_name == l2::RequestQueueName::IMMU
        || queue_name == l2::RequestQueueName::ICF)
    {
        if (bank.in_flight_stores.atomic || fb.pending_atomic(bank_id))
            return {};
    }
    if (queue_name == l2::RequestQueueName::PWC
        && (op->atomic() || bank.in_flight_stores.atomic || fb.pending_atomic(bank_id)))
    {
        if (address_within_bank(op->addr))
            return {};
    }

    if (busses_free && bank.can_schedule(op))
        return {needs_read, needs_write, needs_sys};
    return {};
}

uint64_t L2C::filter_address(PhysAddr addr) const
{
    auto line_bits = banks[0].get_line_bits();
    return addr.extract_range(line_bits + mmu_filter_bits - 1, line_bits);
}

unsigned L2C::in_flight_store_types() const
{
    unsigned flags{0};
    for (unsigned i = 0; i < num_banks; ++i)
        flags |= banks[i].in_flight_stores.store_types();

    return flags;
}

} // namespace ampere::core
