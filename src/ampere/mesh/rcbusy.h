//
// Copyright (c) 2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include "ampere/mesh/chi/opcodes.enum.h"
#include "ampere/sc/counter.h"
#include "ampere/sc/sc.h"
#include "ampere/mesh/cbusy_base.h"

namespace ampere::mesh {

using namespace sc;

class RCBusy : public CBusyBase, poly<RCBusy>
{
public:
    RCBusy(const sc_core::sc_module_name& name);

    void update(chi::CBusyState cbusy, int outstanding_reqs = -1, bool retry = false) override;
    unsigned num_requests() const override { return out_req_lim; }

    Param<bool> enable{"enable", "Enable CBusy adjustments", true};
    Param<std::vector<int>> requester_lut{"requester_lut", "RCBusy LUT", "[]"_json};
    Param<std::vector<int>> throttle{"throttle", "Throttle LUT", "[]"_json};
    Param<uint16_t> throttle_start{"throttle_start", "Start throttling at this rate", 48};
    Param<int8_t> retry_weight{"retry_weight", "Accumulator weight for retry received", 15};
    Param<uint16_t> max_cntr_val{"max_cntr_val", "Max value of saturating counter", 1024};

    void dump(std::ostream& os) const override;

    Event ev_update{"update", "Cbusy reset event occured at requester"};
    Event ev_cbusy_high{"cbusy_high", "Cbusy High received"};
    Event ev_cbusy_medium{"cbusy_medium", "Cbusy Medium received"};
    Event ev_cbusy_low{"cbusy_low", "Cbusy Low received"};
    EventData<unsigned> evd_max_outstanding_requests{
        "max_outstanding_requests",
        "Current maximum number of outstanding requests as adjusted by cbusy throttle events"};
    EventData<unsigned> evd_outstanding_requests{"outstanding_requests",
                                                 "Current number of outstanding requests as supplied by the caller"};
    EventData<unsigned> evd_tracker_occupancy{"tracker_occupancy", "Tracker occupancy"};
    EventData<int> evd_acc_cntr{"acc_cntr", "Accumulator value at requester"};

private:
    uint8_t mask(int cntr, uint8_t high, uint8_t low);

    int counter{0};
    uint8_t current_level{0};
    uint16_t out_req_lim;
};

} // namespace ampere::mesh
