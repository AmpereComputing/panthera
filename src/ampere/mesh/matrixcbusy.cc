//
// Copyright (c) 2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include "ampere/mesh/matrixcbusy.h"
#include <iostream>
#include <algorithm>
#include <limits>

// Matrix CBusy general behavior
//  Completer Side:
//    Maintain a series of <num_cntr> counters covering a time window of <window> cycles each offset by <phase> cycles:
//    | ctr 0 window 0 | ctr 0 window 1 |      ...       | ctr 0 window W |
//        | ctr 1 window 0 | ctr 1 window 1 |      ...       | ctr 1 window W |
//            | ctr 2 window 0 | ctr 2 window 1 |      ...       | ctr 2 window W |
//                ...
//                     | ctr N window N | ctr N window 1 |      ...       | ctr N window W |
//    |<----window---->|
//   >|   |<----phase_cycles
//    |--------time in cycles------->
//
//    Within these counters count any request received (allocations) or completed (deallocations) during each covered
//    time period. Every <phase_cycles> cycles the most recently "completed" window of the given counters are
//    snapshotted and the values reset to 0. An intermediate parameter rho=allocations/deallocations (with special
//    case handling when allocations or deallocations in a given window are 0). Rho is used to index the completer_lut
//    to determine appropriate 3-bit CBusy state value for a given rho. In the real design rho is limited to the 4 high
//    bits of the counter thereby allowing a range of 0.0 to 15.0. Rho scaling is used wtihin the model to ensure that
//    the actual calculated value whose max value depends on the window size is appropriate mapped into rho_max. Max
//    CBusy is sent whenver a RetryAck is experienced and otherwise either the CBusy stated calculated from rho is sent
//    when outstanding requests (reported via api) exceeds <threshold> or optionally an inferred my-completer CBusy
//    state can be returned if it exceeds the computed completer CBusy by using our requester side stats and the DsCBusy
//    LUT
//
//  Requester Side:
//
//    Requester receives 3-bit CBusy and whether or not a response is a RetryAck for all responses. We lookup the
//    returned CBusy value in the <requester_lut> to add the value to the busy-ness accumulator. If response is a
//    RetryAck we also add <retry_weight> to the busy-ness accumulator.

namespace ampere::mesh {

MatrixCBusy::MatrixCBusy(const sc_core::sc_module_name& name) : CBusyBase(name)
{
    req_recv_cntr.resize(num_cntr);
    req_cmp_cntr.resize(num_cntr);
    AMPERE_ASSERT(num_cntr == window / phase_cycles,
                  "window and phase_cycles must be chosen to index exactly num_cntr pairs of counters");
    float last_completer_rho{0.0};
    for (float completer_rho : completer_lut.val()) {
        AMPERE_ASSERT(completer_rho >= last_completer_rho, "Malformed completer lut: range values must be monotonic");
        last_completer_rho = completer_rho;
    }
    rho_scale_factor = rho_max / (float)(window - 1);
    if (throttle_start > 0) {
        out_req_lim = throttle_start;
        qos_demote_lim = throttle_start;
    } else if (enable) {
        AMPERE_ASSERT(throttle.val().size() > 0,
                      "throttle_start must be non-zero or throttle must have at least one value if cbusy is enabled");
        out_req_lim = throttle.val().at(0);

        if (qos_threshold.val().empty() || qos_threshold.val().at(0) == 0)
            qos_demote_lim = out_req_lim;
        else
            qos_demote_lim = (qos_threshold.val().at(0) * out_req_lim) >> 2;
    } else {
        if ((throttle.val().size() > 0) && (throttle.val().at(0) > 0))
            out_req_lim = throttle.val().at(0);
        else
            out_req_lim = std::numeric_limits<decltype(out_req_lim)>::max();
    }

    set_cycle(get_cycle());
}

void MatrixCBusy::incr_cntr(int& cntr)
{
    AMPERE_ASSERT(cntr < window && cntr >= 0, "Counter value is incorrect");
    cntr = (cntr == (window - 1)) ? cntr : cntr + 1;
}

chi::CBusyState MatrixCBusy::update_cbusy(unsigned tracker_occupancy, chi::CBusyState rbusy, bool retry_ack) const
{
    if (!enable)
        return chi::CBusyState::None;

    chi::CBusyState cbusy = chi::CBusyState::None;

    bool cbusy_assigned = false;
    for (uint8_t i = 0; i < completer_lut.val().size() - 1; i++) {
        if (rho_snapshot >= completer_lut.val().at(i) && rho_snapshot < completer_lut.val().at(i + 1)) {
            cbusy = static_cast<chi::CBusyState>(i);
            cbusy_assigned = true;
            break;
        }
    }

    if (!cbusy_assigned)
        AMPERE_FATAL("rho value didnt fall into any bucket in completer_lut");

    if (tracker_occupancy < threshold)
        cbusy = chi::CBusyState::None;

    if (use_highest_cbusy)
        cbusy = std::max(cbusy, rbusy);

    if (retry_ack)
        cbusy = static_cast<chi::CBusyState>(highest_cbusy.val());

    if (!occupancy_busyness.val().empty()) {
        uint8_t i = 0;

        for (auto thresh : occupancy_busyness.val()) {
            if (tracker_occupancy < thresh)
                break;
            ++i;
        }

        cbusy = std::max(cbusy, static_cast<chi::CBusyState>(occupancy_cbusy_value.val().at(i)));
    }

    auto* mthis = const_cast<MatrixCBusy*>(this);
    mthis->num_cbusy.at((int)cbusy)++;

    mthis->ev_cbusy_send(mthis->evd_rho(rho_snapshot * 100.0f),
                         mthis->evd_cbusy_state(static_cast<uint8_t>(cbusy)),
                         mthis->evd_tracker_occupancy(tracker_occupancy));

    return cbusy;
}

uint8_t MatrixCBusy::phase(Cycle cycle) const
{
    return (cycle % window) / phase_cycles;
}

uint64_t MatrixCBusy::phase_delta(Cycle start_cycle, Cycle end_cycle) const
{
    return (end_cycle - start_cycle) / phase_cycles;
}

void MatrixCBusy::refresh_counters(void)
{
    auto current_cycle = get_cycle();
    auto current_phase = phase(current_cycle);

    // Reset any phases which have expired since the last update (which might span multiple phases for in high latency
    // cases)
    auto expired_phases = phase_delta(last_cycle, current_cycle);

    if (expired_phases) {
        if (expired_phases > num_cntr) {
            expired_phases = num_cntr;
        }
        for (int p = current_phase - expired_phases; p < current_phase; p++) {
            reset_counters(p >= 0 ? p : p + num_cntr);
        }

        set_cycle(current_cycle);
    }
}

void MatrixCBusy::reset_counters(uint8_t index)
{
    ev_reset(evd_comp_cbusy_high(num_cbusy.at(7) + num_cbusy.at(6)),
             evd_comp_cbusy_medium(num_cbusy.at(5) + num_cbusy.at(4) + num_cbusy.at(3)),
             evd_comp_cbusy_low(num_cbusy.at(2) + num_cbusy.at(1) + num_cbusy.at(0)));

    float recv_cntr = std::floor(rho_scale_factor * (float)req_recv_cntr.at(index));
    float cmp_cntr = std::floor(rho_scale_factor * (float)req_cmp_cntr.at(index));

    if (cmp_cntr < 1.0) {
        rho_snapshot = recv_cntr > 0.0 ? recv_cntr : 1.0;
    } else {
        rho_snapshot = recv_cntr / cmp_cntr;
    }
    if (rho_snapshot > rho_max) {
        rho_snapshot = rho_max;
    }
    ev_snapshot_rho(evd_rho(rho_snapshot * 100.0f),
                    evd_phase(index),
                    evd_allocates(req_recv_cntr.at(index)),
                    evd_deallocates(req_cmp_cntr.at(index)));
    req_recv_cntr.at(index) = 0;
    req_cmp_cntr.at(index) = 0;
    for (size_t i = 0; i < util::enum_size<chi::CBusyState>(); i++)
        num_cbusy.at(i) = 0;
}

void MatrixCBusy::update_cntr(chi::ReqEvent event, size_t tracker_occupancy)
{
    if (!enable)
        return;

    refresh_counters();

    /* CBusy completer counters */
    for (int i = 0; i < num_cntr; i++) {
        if (event == chi::ReqEvent::ReqRecvEvent)
            incr_cntr(req_recv_cntr.at(i));
        else if (event == chi::ReqEvent::ReqCmpEvent)
            incr_cntr(req_cmp_cntr.at(i));
        else
            AMPERE_FATAL("Unknown event occured");
    }
}

uint8_t MatrixCBusy::mask(int cntr, uint8_t high, uint8_t low)
{
    return ((cntr & util::mask(high, low)) >> low);
}

void MatrixCBusy::update(chi::CBusyState cbusy, int outstanding_reqs, bool retry)
{
    if (!enable)
        return;

    AMPERE_ASSERT(cbusy >= chi::CBusyState::None && cbusy <= chi::CBusyState::Seven, "CBusy value is incorrect");

    counter += requester_lut.val().at(static_cast<uint8_t>(cbusy));

    if (retry)
        counter += retry_weight;

    counter = ((counter > 1023) ? 1023 : ((counter <= 0) ? 0 : counter));
    auto counter_hys = (counter + 32) >= 1023 ? 1023 : (counter + 32);

    uint8_t new_level;
    if (mask(counter, 9, 6) >= current_level || mask(counter_hys, 9, 6) < current_level)
        new_level = mask(counter, 9, 6);
    else
        new_level = current_level;

    AMPERE_ASSERT(new_level >= 0 && new_level < 16, "New_level value is incorrect");

    out_req_lim = throttle.val().at(new_level);
    ds_cbusy = static_cast<chi::CBusyState>(dscbusy.val().at(new_level));

    if (qos_threshold.val().empty() || qos_threshold.val().at(new_level) == 0)
        qos_demote_lim = out_req_lim;
    else
        qos_demote_lim = (qos_threshold.val().at(new_level) * out_req_lim) >> 2;

    current_level = new_level;

    ev_update(evd_max_outstanding_requests(out_req_lim),
              evd_outstanding_requests(outstanding_reqs),
              evd_acc_cntr(counter),
              evd_cbusy_idx(static_cast<uint8_t>(cbusy)),
              evd_retry(retry));

    auto cbusy_val = (unsigned)cbusy;
    if ((cbusy_val == 7) || (cbusy_val == 6))
        ev_cbusy_high();
    else if ((cbusy_val == 5) || (cbusy_val == 4) || (cbusy_val == 3))
        ev_cbusy_medium();
    else
        ev_cbusy_low();
}

void MatrixCBusy::end_of_elaboration()
{
    AMPERE_ASSERT(completer_lut.val().size() <= util::enum_size<chi::CBusyState>() + 1,
                  "CBusy LUT should have no more than {} entries",
                  util::enum_size<chi::CBusyState>() + 1);
}

void MatrixCBusy::dump(std::ostream& os = std::cout) const
{
    os << "CBusy Status:" << std::endl;
    os << "-------------" << std::endl;
    os << "Accumulator counter value: " << (int)counter << std::endl;
    os << "Outstanding Requesters allowed: " << (int)out_req_lim << std::endl << std::endl;
}

} // namespace ampere::mesh
