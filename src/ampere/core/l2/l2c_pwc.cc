//
// Copyright (c) 2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/core/l2/l2c_pwc.h"
#include "ampere/core/l2/l2c_op.h"
#include "ampere/core/l2/l2c_opcode.enum.h"
#include "ampere/uarch_common/memop.h"
#include "ampere/uarch_common/memreq.h"

#include <optional>

namespace ampere::core::l2 {

using namespace uarch_common;

void PWC::allocate(MemReq req, unsigned bank)
{
    bool requires_combine = false;
    auto op_type = l2::Opcode::SUBWR;
    switch (req->atomic_type) {
    case uarch_common::MemOp::ATOMIC_LOAD_WIDE:
        requires_combine = true;
    case uarch_common::MemOp::ATOMIC_LOAD:
        op_type = Opcode::ATOMIC_LOAD;
        break;
    case uarch_common::MemOp::ATOMIC_STORE:
        op_type = Opcode::ATOMIC_STORE;
        break;
    case uarch_common::MemOp::ATOMIC_NONE:
        break;
    }

    auto op = make_clone<l2::Op>(req, op_type, bank, req);
    op->addr = req->phys_addr.line_address();

    const auto& entry = Base::allocate(op, requires_combine);
    op->pwc_id = entry.id;
}

ScheduledOp PWC::schedule(ScheduleCheck okay_to_schedule)
{
    auto reason = drain_reason();
    if (!entries.empty() && reason) {
        auto picked = should_pick_top_entry() ? entries.begin() : pick_hit_entry();
        bool wins_write = okay_to_schedule(queue_name, picked->req).write;
        bool blocked_by_writes = picked->ordered && pending_writes;

        if (wins_write && !blocked_by_writes)
            return schedule_entry(picked, *reason);
    }

    return {};
}

ScheduledOp PWC::schedule_entry(const pwc_iterator& entry, DrainReason reason)
{
    auto op = entry->req;
    op->byte_enables = entry->byte_enables;
    deallocate(entry, reason);
    return {op->atomic(), true, false, op};
}

void PWC::dump(std::ostream& os) const
{
    fmt::print(os, "=== {} ============================================================\n", name());
    fmt::print(os,
               "entries={} request_is_waiting={} age_timer={} ordered_count={} read_hit_count={} lsu_has_barrier={}\n",
               name(),
               entries.size(),
               request_is_waiting,
               age_timer.get_value(),
               ordered_count,
               read_hit_count,
               pending_barrier);

    size_t i = 0;

    for (const auto& entry : entries) {
        fmt::print(os,
                   "[{}]: opcode={} addr={} ordered={} read_hit={}\n",
                   i,
                   entry.req->opcode,
                   entry.req->addr,
                   entry.ordered,
                   entry.read_hit);
        ++i;
    }

    fmt::print(os, "===================================================================\n");
}

} // namespace ampere::core::l2
