//
// Copyright (c) 2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#pragma once

#include "ampere/core/l2/l2c_op.h"
#include "ampere/core/l2/l2c_opcode.enum.h"

namespace ampere::core::l2 {

struct PipelineArbitration
{
    bool read{false};
    bool write{false};
    bool sys{false};

    PipelineArbitration() = default;
    PipelineArbitration(bool read, bool write, bool sys) : read(read), write(write), sys(sys) {}
    bool any() const { return read || write || sys; }
    bool both() const { return read && write; }
};

using ScheduleCheck = std::function<PipelineArbitration(l2::RequestQueueName, l2::Op op)>;

struct ScheduledOp : PipelineArbitration
{
    l2::Op op{nullptr};

    ScheduledOp() = default;
    ScheduledOp(bool read, bool write, bool sys, l2::Op op) : PipelineArbitration(read, write, sys), op(op) {}
};

struct Schedulable
{
    explicit Schedulable(RequestQueueName queue_name) : queue_name{queue_name} {}
    virtual ScheduledOp schedule(ScheduleCheck okay_to_schedule) = 0;

    const RequestQueueName queue_name;
};

class RequestQueue : public Module, public Schedulable
{
public:
    RequestQueue(const sc_core::sc_module_name& name, l2::RequestQueueName queue_name, int depth)
        : Module{name}, Schedulable{queue_name}, depth{depth}
    {
    }

    ScheduledOp schedule(ScheduleCheck okay_to_schedule) override;
    void insert(Op op) { ops.push_back(op); }
    void insert_front(Op op) { ops.push_front(op); }
    unsigned size() const noexcept { return ops.size(); }
    bool empty() const noexcept { return ops.empty(); }
    void clear_pwc_dependencies(uint32_t id);
    void clear_fb_dependencies(int fbid);
    unsigned store_types() const;
    void dump(std::ostream& os) const override;

protected:
    std::list<Op> ops;
    const int depth;
    Cycle last_cycle;
};

} // namespace ampere::core::l2
