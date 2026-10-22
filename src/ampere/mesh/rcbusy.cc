//
// Copyright (c) 2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include "ampere/mesh/rcbusy.h"
#include <iostream>
#include <algorithm>

namespace ampere::mesh {

RCBusy::RCBusy(const sc_core::sc_module_name& name) : CBusyBase(name)
{
    out_req_lim = throttle_start;
    current_level = 0;
}

uint8_t RCBusy::mask(int cntr, uint8_t high, uint8_t low)
{
    return ((cntr & util::mask(high, low)) >> low);
}

void RCBusy::update(chi::CBusyState cbusy, int outstanding_reqs, bool retry)
{
    if (!enable)
        return;

    AMPERE_ASSERT(cbusy >= chi::CBusyState::None && cbusy <= chi::CBusyState::High, "CBusy value is incorrect");
    if (retry)
        counter += retry_weight;
    else
        counter += requester_lut.val().at(static_cast<uint8_t>(cbusy));

    counter = ((counter > 1023) ? 1023 : ((counter <= 0) ? 0 : counter));
    auto counter_hys = (counter + 32) >= 1023 ? 1023 : (counter + 32);

    uint8_t new_level;
    if (mask(counter, 9, 6) >= current_level || mask(counter_hys, 9, 6) < current_level)
        new_level = mask(counter, 9, 6);
    else
        new_level = current_level;

    AMPERE_ASSERT(new_level >= 0 && new_level < 16, "New_level value is incorrect");

    out_req_lim = throttle.val().at(new_level);

    current_level = new_level;

    ev_update(
        evd_max_outstanding_requests(out_req_lim), evd_outstanding_requests(outstanding_reqs), evd_acc_cntr(counter));

    if (cbusy == chi::CBusyState::High)
        ev_cbusy_high();
    else if (cbusy == chi::CBusyState::Medium)
        ev_cbusy_medium();
    else
        ev_cbusy_low();
}

void RCBusy::dump(std::ostream& os = std::cout) const
{
    os << "CBusy Status:" << std::endl;
    os << "-------------" << std::endl;
    os << "Accumulator counter value: " << (int)counter << std::endl;
    os << "Outstanding Requesters allowed: " << (int)out_req_lim << std::endl << std::endl;
}

} // namespace ampere::mesh
