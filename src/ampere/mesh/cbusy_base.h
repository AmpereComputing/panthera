//
// Copyright (c) 2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include "ampere/mesh/chi/opcodes.enum.h"
#include "ampere/sc/sc.h"

namespace ampere::mesh {

using namespace sc;

class CBusyBase : public Module, poly<CBusyBase>
{
public:
    CBusyBase(const sc_core::sc_module_name& name);

    virtual void update(chi::CBusyState cbusy, int outstanding_reqs = -1, bool retry = false);
    virtual unsigned num_requests() const;
    virtual unsigned qos_demote_limit() const { return num_requests(); }

    virtual chi::CBusyState update_cbusy(unsigned outstanding_reqs,
                                         chi::CBusyState rbusy = chi::CBusyState::None,
                                         bool retry_ack = false) const;
    virtual void update_cntr(chi::ReqEvent event, size_t tracker_occupancy);
};

} // namespace ampere::mesh
