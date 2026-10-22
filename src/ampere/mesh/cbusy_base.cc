//
// Copyright (c) 2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include "ampere/mesh/cbusy_base.h"

namespace ampere::mesh {

CBusyBase::CBusyBase(const sc_core::sc_module_name& name) : Module(name) {}

/*********************** Requester Accesors **********************/

void CBusyBase::update(chi::CBusyState cbusy, int outstanding_reqs, bool retry) {}

unsigned CBusyBase::num_requests() const
{
    return 0;
}

/*********************** Completer Accesors **********************/

chi::CBusyState CBusyBase::update_cbusy(unsigned outstanding_reqs, chi::CBusyState rbusy, bool retry_ack) const
{
    return chi::CBusyState::None;
}

void CBusyBase::update_cntr(chi::ReqEvent event, size_t tracker_occupancy) {}

} // namespace ampere::mesh
