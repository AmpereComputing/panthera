//
// Copyright (c) 2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include "ampere/core/l2/l2c_reqqueue.h"

namespace ampere::core::l2 {

ScheduledOp RequestQueue::schedule(ScheduleCheck okay_to_schedule)
{
    // Return OP that can be scheduled
    unsigned d = depth;
    l2::Op tried = nullptr;

    for (auto it = ops.begin(); it != ops.end() && d != 0; it++, --d) {
        auto op = *it;

        /* This operation is dependent on a pwc to drain, so don't schedule again until
         *  pwc drains */
        if (op->pwc_id != 0 || op->replay_after_fbid) {
            tried = (!tried && d == 1) ? op : tried;
            continue;
        }

        auto [wins_read, wins_write, wins_sys] = okay_to_schedule(queue_name, op);

        if (wins_read || wins_write || wins_sys) {
            ops.erase(it);
            return {wins_read, wins_write, wins_sys, op};
        }

        tried = op;
    }
    return {false, false, false, tried};
}

void RequestQueue::clear_pwc_dependencies(uint32_t id)
{
    for (auto& op : ops) {
        if (op->pwc_id == id)
            op->pwc_id = 0;
    }
}

void RequestQueue::clear_fb_dependencies(int fbid)
{
    for (auto& op : ops) {
        if (op->replay_after_fbid && *op->replay_after_fbid == fbid)
            op->replay_after_fbid = std::nullopt;
    }
}

unsigned RequestQueue::store_types() const
{
    unsigned flags{0};
    for (auto& op : ops)
        flags |= op->req->store_types();

    return flags;
}

void RequestQueue::dump(std::ostream& os) const
{
    if (!ops.empty()) {
        fmt::print(os, "{}:\n", name());
        fmt::print(os, " Opcode     |       Addr       | Bank | FBID |\n");
        fmt::print(os, "==============================================\n");
        for (auto op : ops)
            op->dump(os);
        fmt::print(os, "\n");
    }
}

} // namespace ampere::core::l2
