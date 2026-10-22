//
// Copyright (c) 2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include "ampere/core/pwc.h"
#include "ampere/core/l2/l2c_reqqueue.h"
#include "ampere/uarch_common/memreq.h"

namespace ampere::core::l2 {

using namespace archx;
using namespace sc;
using namespace uarch_common;

// L2C Partial Write Combiner
class PWC : public core::BasicPWC<Op>, public Schedulable
{
    using Base = core::BasicPWC<Op>;
    using pwc_iterator = Base::pwc_iterator;

public:
    explicit PWC(const sc_core::sc_module_name& name) : Base{name}, Schedulable{RequestQueueName::PWC} {}

    void allocate(MemReq req, unsigned bank);
    ScheduledOp schedule(ScheduleCheck okay_to_schedule) override;

    void dump(std::ostream&) const override;

private:
    ScheduledOp schedule_entry(const pwc_iterator& entry, DrainReason reason);
};

} // namespace ampere::core::l2
