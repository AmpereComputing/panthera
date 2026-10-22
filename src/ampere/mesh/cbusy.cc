//
// Copyright (c) 2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include "ampere/mesh/cbusy.h"
#include <iostream>

namespace ampere::mesh {

CBusy::CBusy(const sc_core::sc_module_name& name) : CBusyBase(name)
{
    AMPERE_ASSERT(c_threshold_high >= c_threshold_mid, "c_threshold_high not >= c_threshold_mid");
    AMPERE_ASSERT(c_threshold_mid >= c_threshold_low, "c_threshold_mid not >= c_threshold_low");
}

void CBusy::reset_window(int outstanding_reqs)
{
    if (outstanding_reqs >= 0)
        ev_reset(evd_high(high_busy),
                 evd_low(low_busy),
                 evd_outstanding_requests(outstanding_reqs),
                 evd_max_outstanding_requests(c_outstanding.get_value()));
    else
        ev_reset(evd_high(high_busy), evd_low(low_busy), evd_max_outstanding_requests(c_outstanding.get_value()));

    low_busy = 0;
    high_busy = 0;
    window_count = 0;
}

chi::CBusyState CBusy::update_cbusy(unsigned outstanding_reqs, chi::CBusyState rbusy, bool retry_ack) const
{
    if (outstanding_reqs >= c_threshold_high) {
        return c_high_state;
    } else if (outstanding_reqs >= c_threshold_mid) {
        return c_mid_state;
    }

    return c_low_state;
}

void CBusy::update(chi::CBusyState cbusy, int outstanding_reqs, bool retry)
{
    if (!enable)
        return;

    window_count++;

    switch (cbusy) {
    case chi::CBusyState::None: /* Not Busy, Throttle Up */
        low_busy++;
        break;
    case chi::CBusyState::Low: /* Low Busy, Throttle Up */
        low_busy++;
        break;
    case chi::CBusyState::Medium: /* Medium Busy, Hold Level */
        break;
    case chi::CBusyState::High:  /* High Busy, Throttle Down */
    case chi::CBusyState::Four:  /* High Busy, Throttle Down */
    case chi::CBusyState::Five:  /* High Busy, Throttle Down */
    case chi::CBusyState::Six:   /* High Busy, Throttle Down */
    case chi::CBusyState::Seven: /* High Busy, Throttle Down */
        high_busy++;
        break;
    };

    /* Window has closed, time to trigger a throttle event */
    if (window_count >= window) {
        /* Note: Both low_busy and high_busy can occur at the same tiem
         * in this case throttle down overrides throttle ups */

        /* Action Throttle Up */
        if (low_busy >= threshold_up && high_busy < threshold_down) {
            bool throttling = c_outstanding.get_value() < throttle_max;
            c_outstanding += step_up;
            c_prefetcher = true;
            if (throttling)
                ev_throttle(evd_max_outstanding_requests(c_outstanding.get_value()),
                            evd_prefetcher_enabled(c_prefetcher));
        }

        /* Action Throttle Down */
        if (high_busy >= threshold_down) {
            bool throttling = c_outstanding.get_value() > throttle_min;
            c_prefetcher = false;
            c_outstanding -= step_down;
            if (throttling)
                ev_throttle(evd_max_outstanding_requests(c_outstanding.get_value()),
                            evd_prefetcher_enabled(c_prefetcher));
        }

        reset_window(outstanding_reqs);
    }
}

void CBusy::dump(std::ostream& os = std::cout) const
{
    os << "CBusy Status:" << std::endl;
    os << "-------------" << std::endl;
    os << "Prefetcher enabled: " << c_prefetcher << std::endl;
    os << "Outstanding Requesters allowed: " << c_outstanding.get_value() << std::endl << std::endl;
}

} // namespace ampere::mesh
